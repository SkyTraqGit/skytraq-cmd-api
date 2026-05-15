#!/bin/bash
# run_tests.sh - end-to-end test of gnss_tool against the fake GNSS receiver
set -u
cd "$(dirname "$0")"

PASS=0
FAIL=0

# Start the fake receiver in the background; it prints the PTY path to stdout.
PTY_FILE=$(mktemp)
python3 fake_gnss.py > "$PTY_FILE" &
FAKE_PID=$!
# Wait for the fake to print its PTY path.
for i in $(seq 1 50); do
    sleep 0.05
    if [ -s "$PTY_FILE" ]; then break; fi
done
PTY=$(cat "$PTY_FILE")
if [ -z "$PTY" ]; then
    echo "FATAL: fake_gnss.py did not produce PTY path"
    kill $FAKE_PID 2>/dev/null
    exit 1
fi
echo "Fake GNSS pty: $PTY (pid $FAKE_PID)"
echo

cleanup() { kill $FAKE_PID 2>/dev/null; rm -f "$PTY_FILE"; }
trap cleanup EXIT

run() {
    local name="$1"; shift
    local want="$1"; shift  # substring expected in output
    echo "=== TEST: $name ==="
    out=$("$@" 2>&1)
    echo "$out" | sed 's/^/  | /'
    if echo "$out" | grep -q -- "$want"; then
        echo "  ✓ PASS"
        PASS=$((PASS+1))
    else
        echo "  ✗ FAIL (expected to find: $want)"
        FAIL=$((FAIL+1))
    fi
    echo
}

# Use a short timeout in tests so failures are fast.
T="-t 1500"

run "Query software version (-q 2 0)" \
    "Kernel Version" \
    ./gnss_tool -p "$PTY" $T -q 0x02 0

run "Query software CRC (-q 3 1)" \
    "CRC " \
    ./gnss_tool -p "$PTY" $T -q 3 1

run "Query position update rate (decimal id)" \
    "Update Rate" \
    ./gnss_tool -p "$PTY" $T -q 16 0

run "Query NMEA talker ID (hex with 'h' suffix)" \
    "Talker ID type" \
    ./gnss_tool -p "$PTY" $T -q 4Fh 0

run "Configure NMEA output (-c) -> ACK" \
    "ACK received" \
    ./gnss_tool -p "$PTY" $T -c 0x09 1 1

run "Configure with bad value -> NACK" \
    "NACK received" \
    ./gnss_tool -p "$PTY" $T -c 0x0E 99 1

run "Sub-ID query (100/24 nav mode)" \
    "Navigation mode" \
    ./gnss_tool -p "$PTY" $T -q 100/24 0

run "Sub-ID query (0x64/0x20 GPS time)" \
    "TOW" \
    ./gnss_tool -p "$PTY" $T -q 0x64/0x20 0

run "Save raw response to file (-o)" \
    "saved" \
    ./gnss_tool -p "$PTY" $T -o /tmp/swver.bin -q 0x02 0

if [ -f /tmp/swver.bin ]; then
    echo "=== Saved file content (xxd): ==="
    xxd /tmp/swver.bin | sed 's/^/  | /'
    echo
fi

# Batch script test
cat > /tmp/script.txt <<EOF
# comment line
-q 2 0
-c 9 1 1
-q 100/24 0
EOF
run "Batch script (-i)" \
    "script line 3" \
    ./gnss_tool -p "$PTY" $T -i /tmp/script.txt

run "Unknown ID via -h" \
    "Unknown" \
    ./gnss_tool -h 0xFE

run "Help for sub-ID message" \
    "0x17" \
    ./gnss_tool -h 100/23

run "Unknown-to-DB message sent as raw bytes" \
    "not in the DB" \
    ./gnss_tool -p "$PTY" $T -c 0xFE 0 1

echo "===================================="
echo "Total: $((PASS+FAIL))   Pass: $PASS   Fail: $FAIL"
[ "$FAIL" -eq 0 ]
