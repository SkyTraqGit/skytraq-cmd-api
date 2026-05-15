# Contributing to Skytraq Command API

Thanks for your interest in contributing. This document explains how to
report issues, propose changes, and submit code.

## Reporting Issues

Before opening a new issue, please search existing ones to avoid duplicates.

When reporting a bug, include:

- SkyTraq receiver model / firmware version (output of `gnss_tool -q 0x02 0`
  is ideal).
- Operating system and version (`uname -a` on Linux, or the MSYS2 / Windows
  version on Windows).
- Compiler and version (`gcc --version`).
- The exact `gnss_tool` command line you ran.
- The serial port settings (port, baud rate).
- Observed vs. expected behavior, including any ACK / NACK / Timeout output.
- If possible, attach the raw response frame saved with `-o FILE`, or a
  capture from a logic analyzer / serial sniffer.

For feature requests — especially adding new messages to the curated
database — describe the use case and reference the relevant section of
AN0037 if you can.

## Development Workflow

1. Fork the repository and create a topic branch:
   ```bash
   git checkout -b feat/add-msg-0x67
   ```

2. Build locally and confirm there are no new warnings:
   ```bash
   make clean
   make CFLAGS="-O2 -Wall -Wextra -Wpedantic -std=c99"
   ```

3. Run the fake-receiver test suite:
   ```bash
   ./run_tests.sh
   ```
   All tests should pass before you open a PR.

4. If your change touches real-hardware behavior (serial backend, framing,
   timing), please also run a relevant subset of `real_tests.py` against an
   actual receiver and include the result in the PR description.

5. Commit with a [Conventional Commits](https://www.conventionalcommits.org/)
   prefix:
   - `feat:` new functionality (a new message, a new CLI flag, etc.)
   - `fix:` a bug fix
   - `docs:` documentation only
   - `refactor:` no functional change
   - `test:` test-only changes
   - `chore:` build, CI, tooling

6. Push and open a pull request against `main`. Link any related issues.

## Coding Style

- C99, compiled with `-Wall -Wextra` cleanly.
- 4-space indent, no tabs in C source, lines up to ~100 columns.
- Public functions and message-table entries get a short comment explaining
  the AN0037 section they implement.
- Keep platform-specific code inside `serial.c` and behind `#ifdef _WIN32` /
  POSIX guards. The rest of the codebase should remain portable.
- No dynamic allocation in `protocol.c` — frames live on the stack.

## Adding a New Message

The most common contribution is extending the message database. The pattern
in `messages.c` is:

1. Add a `static const skytraq_field_t fields_<name>[]` array describing
   the fields (name, width in bytes, type, unit).
2. Add a `skytraq_msg_def_t` entry to the appropriate table (`message_defs`
   or `submsg_defs`).
3. If the message is a query, link its response message ID so `-q` can
   wait for the right frame.
4. Add a row to the "Coverage of the AN0037 message set" section of
   `README.md`.
5. Add a fake-receiver responder branch to `fake_gnss.py` and a test case
   to `run_tests.sh` if practical.

Copy any existing entry and adapt — the structure is straightforward.

## Commit Hygiene

- Keep commits focused; rebase to squash "fix typo" commits before merge.
- The PR description should explain **what** and **why**, not just **how**.
- Mark breaking changes with `!` in the Conventional Commits subject
  (e.g. `refactor!: rename -o flag to --output`) and describe the migration
  in the PR body.

## License of Contributions

By submitting a pull request, you agree that your contribution will be
licensed under the project's MIT License (see [LICENSE](LICENSE)).
