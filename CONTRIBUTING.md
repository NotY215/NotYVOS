# Contributing to NotYVOS

Thanks for your interest in NotYVOS. Contributions, bug reports, documentation fixes, and thoughtful feedback are welcome.

## Before you start

- Read the [README](README.md), [build instructions](docs/BUILDING.md), and [architecture notes](docs/ARCHITECTURE.md) relevant to your change.
- Check existing issues and pull requests to avoid duplicating work.
- For substantial changes, open an issue or discussion first so the approach can be agreed before significant work begins.
- Follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## How to contribute

1. Fork the repository and create a focused branch.
2. Make the smallest coherent change that solves the problem.
3. Keep code and documentation consistent with the current implementation. Do not describe planned features as already working.
4. Add or update tests when the project provides an appropriate way to test the change.
5. Run relevant build, test, formatting, or QEMU checks available for the affected area. In your pull request, state what you ran and what you could not run.
6. Update documentation when behavior, controls, build steps, dependencies, or limitations change.
7. Open a pull request with a clear summary, rationale, testing notes, and any relevant screenshots or logs. Remove secrets and personal data from logs.

## Bug reports

Use GitHub Issues at https://github.com/NotY215/NotYVOS/issues. Include reproduction steps, expected and actual behavior, relevant environment details, and concise logs where useful. Search for an existing report first. Do not include passwords, access tokens, private keys, or unrelated personal information.

## Scope and design

NotYVOS is an early-stage rewrite. Current behavior and limitations in the source code and README take precedence over assumptions based on a future roadmap. Avoid unrelated refactors in a bug-fix pull request. Preserve the project's independent identity and do not add proprietary console firmware, copyrighted console assets, or other material without the necessary rights.

## Licensing

By submitting a contribution, you agree that it may be distributed under the repository's existing license, GNU Affero General Public License version 3 (AGPL-3.0), unless a different arrangement is explicitly agreed in writing. You confirm that you have the right to submit the contribution and identify any third-party code or assets you include. This paragraph does not replace the license text or provide legal advice.

## Review

Maintainers may ask for changes, additional tests, or clarification. A pull request may be declined if it is out of scope, unsafe, legally problematic, insufficiently tested, or inconsistent with the project's goals.
