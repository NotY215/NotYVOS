# Third-Party Licenses

NOTYVOS itself is licensed under **AGPL-3.0-or-later**.

Third-party components retain their **own** licenses. NOTYVOS does not
require third-party components to be AGPL-licensed. Each component's
license is listed below and its full text is stored in this directory.

## Components bundled in the NOTYVOS repository

| Component | Version | License | File |
|---|---|---|---|
| Limine | v12.9.0 | BSD-2-Clause | `limine.txt` |

## Applications distributed as installers (not part of the OS source)

| Application | Distribution | License | Notes |
|---|---|---|---|
| Brave Browser | `Inbuilt_Soft/brave_installer-win64.exe` | MPL-2.0 | Browser code open source under MPL-2.0. |
| VLC Media Player | `Inbuilt_Soft/vlc-3.0.23-win64.exe` | GPLv2-or-later (player), LGPLv2-or-later (libVLC) | See `vlc.txt`. |

These installers are user-space application packages. They are NOT kernel
components. They are NOT part of NOTYVOS source code. Their licenses are
independent of NOTYVOS's AGPL-3.0 license.

## User-provided components (never committed)

| Component | Source | License |
|---|---|---|
| PS3 firmware module | User-supplied, legally obtained | Sony proprietary |
| PS3 game software | User-supplied, legally obtained | Respective copyright holders |
| Windows applications | User-supplied | Respective copyright holders |

## Compatibility rule

Any third-party component added to the NOTYVOS repository must have a
license compatible with **AGPL-3.0-or-later** for the manner in which it
is used (linked, bundled, or distributed together).

- BSD-2-Clause (Limine): compatible.
- MPL-2.0 (Brave browser code): compatible for separate-process use.
- GPLv2-or-later (VLC): compatible; if linked into the kernel tree,
  the combined work remains AGPL-3.0-or-later.
- Proprietary firmware: not linked, not redistributed; user-supplied only.

If you are unsure whether a component is compatible, open an ADR before
adding it.
