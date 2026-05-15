#!/usr/bin/env python3
"""
real_tests.py - Run gnss_tool tests defined in a Markdown plan file.

Cross-platform: works on Linux and on Windows MSYS2 UCRT64 (uses
gnss_tool.exe automatically when present).

The plan file is regular Markdown. Test cases are written as fenced code
blocks tagged ```test, with simple key:value lines inside. Everything
outside ```test blocks (headings, paragraphs, other code blocks) is
ignored, so the file doubles as human-readable documentation.

Supported keys (one per line, "key: value", case-insensitive):

    name        Test display name. Required.
    type        query | configure | help        Required.
                  query     -> gnss_tool -q ...
                  configure -> gnss_tool -c ...
                  help      -> gnss_tool -h ...   (no port needed)
    msgid       Message ID. Required. Same numeric formats as gnss_tool:
                  123, 7Bh, 0x7B, 100/23, 0x64/0x17, ...
    fields      Space-separated field values. Optional.
    expect      Substring or extended-regex pattern to look for in
                gnss_tool's output. If omitted, defaults to
                  "Response received" for query
                  "ACK received"      for configure
                  "Field"             for help
    nack_is     skip | pass | fail          (default: skip)
                What to do when a NACK is received. "skip" treats it as
                "feature not supported on this firmware".
    outfile     Save raw response frame to this path (passed to -o).
    timeout_ms  Per-test response timeout in ms. Defaults to global -t.
    baud        Per-test baud override (rare; e.g. after a 0x05 reset).
    delay_ms    Wait this many milliseconds AFTER this test before
                running the next one. Default 0.
    repeat      How many response frames to collect after the ACK.
                  1   = single response (default; matches old behaviour)
                  N>1 = exactly N frames, or until timeout
                  0   = collect every frame until timeout (streaming mode,
                        e.g. 0x5B with slot=0 dumps all 24 GLONASS slots)
                When -o is set with repeat>1 or 0, every captured frame
                is appended to the output file.

Usage:
    real_tests.py PLAN.md -p PORT [-b BAUD] [-t TIMEOUT_MS] [--dry-run]

Examples:
    real_tests.py plan.md -p COM3
    real_tests.py plan.md -p /dev/ttyUSB0 -b 115200 -t 3000
    real_tests.py plan.md --dry-run                 # parse only, no I/O

Exit code:
    0  every test PASSed (skips don't count as failures)
    1  one or more tests FAILed
    2  could not parse the plan or could not run gnss_tool
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
import time

# ---------- Markdown plan parser ------------------------------------------------

FENCE_RE = re.compile(r'^\s*```\s*test\s*$', re.IGNORECASE)
END_FENCE_RE = re.compile(r'^\s*```\s*$')

def parse_plan(path):
    """Yield dicts, one per ```test block, in order. Raises on malformed input."""
    tests = []
    with open(path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    i = 0
    block_no = 0
    while i < len(lines):
        if FENCE_RE.match(lines[i]):
            start_line = i + 1   # 1-based
            i += 1
            body = []
            while i < len(lines) and not END_FENCE_RE.match(lines[i]):
                body.append(lines[i])
                i += 1
            if i >= len(lines):
                raise ValueError(f"{path}:{start_line}: unterminated ```test block")
            block_no += 1
            tests.append(_parse_block(path, start_line, block_no, body))
            i += 1   # past closing fence
        else:
            i += 1
    return tests

def _parse_block(path, start_line, block_no, body):
    fields = {}
    for offset, raw in enumerate(body):
        line = raw.rstrip('\n')
        # blank or comment line inside block
        s = line.strip()
        if not s or s.startswith('#'):
            continue
        if ':' not in line:
            raise ValueError(
                f"{path}:{start_line + offset}: expected 'key: value', got: {line!r}")
        key, _, val = line.partition(':')
        key = key.strip().lower()
        val = val.strip()
        if key in fields:
            raise ValueError(
                f"{path}:{start_line + offset}: duplicate key '{key}' in block #{block_no}")
        fields[key] = val

    # required keys
    for req in ('name', 'type', 'msgid'):
        if req not in fields:
            raise ValueError(
                f"{path}:{start_line}: block #{block_no} is missing required key '{req}'")
    if fields['type'].lower() not in ('query', 'configure', 'help'):
        raise ValueError(
            f"{path}:{start_line}: block #{block_no} has invalid type "
            f"'{fields['type']}' (expected query/configure/help)")
    if 'nack_is' in fields and fields['nack_is'].lower() not in ('skip','pass','fail'):
        raise ValueError(
            f"{path}:{start_line}: block #{block_no} has invalid nack_is "
            f"'{fields['nack_is']}' (expected skip/pass/fail)")
    fields['_source'] = f"{path}:{start_line}"
    fields['_block_no'] = block_no
    return fields

# ---------- Tool location ------------------------------------------------------

def find_tool(script_dir):
    for name in ('gnss_tool.exe', 'gnss_tool'):
        p = os.path.join(script_dir, name)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    raise FileNotFoundError(
        f"gnss_tool / gnss_tool.exe not found in {script_dir}; run 'make' first")

# ---------- Test runner --------------------------------------------------------

def build_argv(tool, test, port, baud, timeout_ms):
    """Build the gnss_tool argv list for one test."""
    argv = [tool]
    ttype = test['type'].lower()

    if ttype == 'help':
        # -h doesn't need a port
        argv += ['-h', test['msgid']]
        return argv

    # query / configure - need port
    argv += ['-p', port]
    argv += ['-b', str(test.get('baud', baud))]
    argv += ['-t', str(test.get('timeout_ms', timeout_ms))]
    if 'repeat' in test:
        try:
            r = int(test['repeat'])
        except ValueError:
            raise ValueError(f"{test['_source']}: 'repeat' must be an integer, got {test['repeat']!r}")
        argv += ['-r', str(r)]
    if 'outfile' in test:
        argv += ['-o', test['outfile']]
    flag = '-q' if ttype == 'query' else '-c'
    argv += [flag, test['msgid']]
    if test.get('fields'):
        # split on whitespace - shlex preserves quoted strings if anyone wants them
        argv += shlex.split(test['fields'])
    return argv

def default_expect(test):
    t = test['type'].lower()
    if t == 'query':
        # When repeat != 1, the tool prints "Response #N received" or
        # "Collected N response frame(s)". Match either.
        if 'repeat' in test and str(test['repeat']) != '1':
            return r'(Response #1 received|Collected [0-9]+ response)'
        return r'Response received'
    if t == 'configure': return r'ACK received'
    if t == 'help':      return r'Field'
    return r''

def classify(test, output, returncode):
    """Decide PASS/FAIL/SKIP for one test based on its output.
    Returns (verdict, reason) where verdict in {'PASS','FAIL','SKIP'}."""
    expect = test.get('expect', default_expect(test))
    nack_is = test.get('nack_is', 'skip').lower()

    if expect:
        try:
            if re.search(expect, output):
                return ('PASS', f'matched /{expect}/')
        except re.error as e:
            return ('FAIL', f'invalid regex in expect: {e}')

    # No match. Diagnose why.
    if 'NACK received' in output:
        if nack_is == 'pass': return ('PASS', 'NACK received (nack_is=pass)')
        if nack_is == 'fail': return ('FAIL', 'NACK received (nack_is=fail)')
        return ('SKIP', 'NACK received (feature not supported by firmware)')
    if 'Timeout' in output or 'timeout' in output:
        return ('FAIL', 'timeout - no response from receiver')
    if returncode != 0:
        return ('FAIL', f'gnss_tool exited with code {returncode}')
    return ('FAIL', f'expected pattern not found: /{expect}/')

def run_test(tool, test, port, baud, timeout_ms, dry_run=False, indent='      '):
    argv = build_argv(tool, test, port, baud, timeout_ms)
    pretty = ' '.join(shlex.quote(a) for a in argv)
    print(f"=== TEST: {test['name']} ===")
    print(f"    cmd: {pretty}")

    if dry_run:
        print(f"{indent}(dry-run; not executing)\n")
        return ('PASS', 'dry-run')

    try:
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=60)
        output = (proc.stdout or '') + (proc.stderr or '')
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        return ('FAIL', 'process-level timeout (60s)')
    except FileNotFoundError as e:
        return ('FAIL', f'cannot exec gnss_tool: {e}')

    for line in output.splitlines():
        print(f"{indent}{line}")

    verdict, reason = classify(test, output, rc)
    return verdict, reason

# ---------- main ---------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(
        description='Run gnss_tool tests defined in a Markdown plan file.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='See the top of this script for the full plan file format.')
    p.add_argument('plan', help='Markdown plan file containing ```test blocks')
    p.add_argument('-p', '--port', help='serial port (e.g. COM3, /dev/ttyUSB0). '
                                        'Required unless every test has type=help.')
    p.add_argument('-b', '--baud', type=int, default=115200,
                   help='default baud rate (default: 115200)')
    p.add_argument('-t', '--timeout', type=int, default=3000,
                   help='default response timeout in ms (default: 3000)')
    p.add_argument('--dry-run', action='store_true',
                   help='parse the plan and print commands, but do not run them')
    p.add_argument('--tool', help='path to gnss_tool executable (auto-detected by default)')
    args = p.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    try:
        tool = args.tool or find_tool(script_dir)
    except FileNotFoundError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    try:
        tests = parse_plan(args.plan)
    except (ValueError, FileNotFoundError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    if not tests:
        print(f"WARNING: no ```test blocks found in {args.plan}", file=sys.stderr)
        return 0

    needs_port = any(t['type'].lower() != 'help' for t in tests)
    if needs_port and not args.port and not args.dry_run:
        print("ERROR: --port is required (the plan contains query/configure tests)",
              file=sys.stderr)
        return 2

    # Header
    print(f"Plan:    {args.plan}  ({len(tests)} test(s))")
    print(f"Tool:    {tool}")
    if needs_port:
        print(f"Port:    {args.port or '(none - dry-run)'}")
        print(f"Baud:    {args.baud}")
        print(f"Timeout: {args.timeout} ms")
    if args.dry_run:
        print("Mode:    DRY-RUN (commands shown but not executed)")
    print()

    pass_n = fail_n = skip_n = 0
    results = []

    for idx, test in enumerate(tests):
        verdict, reason = run_test(tool, test, args.port, args.baud,
                                   args.timeout, dry_run=args.dry_run)
        if verdict == 'PASS':
            print(f"    [OK]   PASS  ({reason})")
            pass_n += 1
        elif verdict == 'SKIP':
            print(f"    [--]   SKIP  ({reason})")
            skip_n += 1
        else:
            print(f"    [FAIL] FAIL  ({reason})")
            fail_n += 1
        results.append((test['name'], verdict, reason))

        # delay before next test (skip after the last one)
        if idx < len(tests) - 1:
            try:
                d = int(test.get('delay_ms', 0))
            except ValueError:
                print(f"    WARN: ignoring non-integer delay_ms={test['delay_ms']!r}",
                      file=sys.stderr)
                d = 0
            if d > 0:
                if not args.dry_run:
                    print(f"    ... waiting {d} ms before next test")
                    time.sleep(d / 1000.0)
                else:
                    print(f"    ... (would wait {d} ms before next test)")
        print()

    # Summary
    print("=" * 64)
    print(f"Total: {len(tests)}   Pass: {pass_n}   Fail: {fail_n}   Skip: {skip_n}")
    print("=" * 64)
    # Concise per-test recap
    for name, verdict, reason in results:
        marker = {'PASS':'[OK]  ', 'FAIL':'[FAIL]', 'SKIP':'[--]  '}[verdict]
        print(f"  {marker}  {name}  -- {reason}")

    return 0 if fail_n == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
