# Security Policy

## Supported Versions

Only the latest tagged release receives security fixes.

| Version | Supported |
| ------- | --------- |
| 1.x     | ✅        |
| < 1.0   | ❌        |

## Reporting a Vulnerability

If you believe you have found a security vulnerability in this project,
**please do not open a public GitHub issue.**

Instead, report it privately via GitHub's
[private vulnerability reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability)
feature on this repository (Security → Report a vulnerability).

When reporting, please include:

- A description of the issue and its potential impact.
- Steps to reproduce, ideally with a minimal command line and the
  expected vs. observed behavior.
- The version, commit hash, OS, and compiler you tested on.
- Any suggested fix, if you have one.

We will acknowledge receipt within **7 days** and aim to provide an initial
assessment within **14 days**. Coordinated disclosure timelines will be
agreed with the reporter.

## Scope

This project is a userspace command-line tool that opens serial ports and
parses bytes from an attached GNSS receiver. The most relevant classes of
vulnerability are:

- Buffer over-reads or over-writes when parsing malformed frames.
- Integer overflow / sign issues in length or checksum handling.
- Path-traversal or arbitrary-write bugs in `-o FILE` / `-i SCRIPT`.
- Privilege-escalation surface created by serial-port group membership
  (out of scope unless the tool itself misbehaves with elevated
  privileges).

Hardening of the underlying OS serial stack, the toolchain, and the
GNSS receiver firmware is **out of scope**.
