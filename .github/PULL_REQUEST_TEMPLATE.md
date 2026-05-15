<!--
Thanks for sending a pull request! Please make sure you've read CONTRIBUTING.md.
-->

## Summary

<!-- What does this PR do and why? Link related issues with "Fixes #N". -->

## Type of change

- [ ] Bug fix (non-breaking)
- [ ] New feature (non-breaking)
- [ ] Breaking change
- [ ] Documentation only
- [ ] Build / CI / tooling

## Checklist

- [ ] `make clean && make` builds cleanly (no new warnings) on Linux.
- [ ] `./run_tests.sh` passes against `fake_gnss.py`.
- [ ] If this touches the serial backend or framing, I have tested against
      real hardware and described the result below.
- [ ] If this adds a new message, `README.md` and `messages.c` are both updated,
      and a test case is added to `fake_gnss.py` / `run_tests.sh`.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.

## Hardware test results (if applicable)

<!-- Receiver model, firmware, command line, and what you observed. -->
