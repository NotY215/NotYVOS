# ADR 0003 — AGPL-3.0-or-later license for NOTYVOS

Status: Accepted
Date: 2025

## Context
The NOTYVOS project itself requires a strong copyleft license that covers
network use. AGPL-3.0 was selected. Third-party components are NOT required
to be AGPL-licensed; each retains its own license.

## Decision
NOTYVOS source code is licensed under the GNU Affero General Public License,
version 3.0

Third-party components retain their own licenses:
- Limine: BSD-2-Clause
- Brave Browser: MPL-2.0
- VLC Media Player: GPLv2-or-later / LGPLv2-or-later
- PS3 firmware: Sony proprietary (user-supplied, never redistributed)

Third-party components are listed in `THIRD_PARTY_LICENSES/` with their
license texts.

## Consequences
- Any network service built from NOTYVOS source must offer corresponding
  source to users (AGPL §13).
- BSD/MIT/Apache-2.0/MPL-2.0 components are compatible with AGPL-3.0
  when used as separate processes or linked in a permitted manner.
- GPLv2+ components (VLC) are compatible; if linked into the kernel tree,
  the combined work remains AGPL-3.0-or-later.
- User-space applications that link AGPL kernel headers inherit AGPL
  obligations. A separate closed-source user-space policy would require
  architectural separation (stable ABI + no AGPL headers in that ABI).
- Sony firmware, Brave, and VLC installers are not part of NOTYVOS source
  and are unaffected by NOTYVOS's license.

## Alternatives considered
- Apache-2.0: permissive, but allows proprietary forks and closed network
  deployments. Rejected.
- GPL-3.0: strong copyleft, but silent on network use. Rejected.
- MIT: rejected for the same reason as Apache-2.0.
