# Privacy

## Scope

This document describes privacy considerations for the NotYVOS source repository and the project as currently documented. It is not a guarantee about every future build, dependency, hosting service, or third-party application.

## Repository and project communications

When you use GitHub to browse the source, open issues, submit pull requests, or participate in discussions, GitHub processes information under its own privacy policy and settings. Public issues, pull requests, commits, and profile information may be visible to others.

Do not publish passwords, authentication tokens, private keys, personal addresses, or other sensitive information in this repository or its issue tracker.

## The operating system

NotYVOS is an early-stage rewrite. Its currently documented interface does not implement media playback or networking features. This statement is not a promise that every dependency or future build performs no network activity or collects no data. Behavior can change as components and features are added.

Review the source code, build configuration, included dependencies, and any third-party services before using a build with sensitive data. A future release that adds telemetry, accounts, networking, or data collection should document those behaviors, purposes, and available controls before release.

## Data and security

Avoid placing confidential files on test systems. QEMU images, logs, screenshots, and bug reports may contain information from your environment. Review them before sharing.

## Changes

This policy may be updated as NotYVOS develops. The policy on this repository does not override GitHub's policies or the privacy terms of external services.
