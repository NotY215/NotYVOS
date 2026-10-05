# Third-Party Licenses

NOTYVOS itself is licensed under AGPL-3.0-or-later.

Third-party components retain their own licenses. This directory records the
components currently present or used by the repository.

## Components bundled or fetched for the project

| Component | Version | License | Location / role |
|---|---|---|---|
| Limine | v12.9.0 | BSD-2-Clause | third_party/limine/ |
| Inter font family | bundled upstream release | SIL OFL-1.1 | Fonts/ |
| Brave Browser installer | win64 package | MPL-2.0 | Inbuilt Devices/ |
| VLC Media Player installer | 3.0.23 win64 package | GPLv2+ / LGPLv2+ | Inbuilt Devices/ |

The Brave and VLC files are application installers. They are not kernel components and are not linked into the kernel. Their validation is explicitly excluded from the current NOTYVOS roadmap.

## Development and testing tools

| Tool | License | Role |
|---|---|---|
| Oracle VirtualBox | GPL-3.0-or-later | Current VM test environment |
| QEMU | GPL-2.0 | Retained only as legacy/build configuration data |

VirtualBox is not shipped as part of the OS. QEMU is not part of the active
runtime test workflow.

## User-provided components

| Component | Source / status |
|---|---|
| PS3 firmware | User-supplied, legally obtained, Sony proprietary |
| PS3 game software | User-supplied, respective copyright holders |
| Windows applications | User-supplied, respective copyright holders; compatibility validation excluded from current roadmap |

NOTYVOS does not redistribute Sony private keys, decryption keys, or
decryption bypass tooling.

## License files

- Limine: limine.txt
- Inter: inter-font.txt
- VLC: vlc.txt
- VirtualBox: virtualbox.txt

The Brave installer is documented as a separate application package; its
license is not a kernel dependency.

Third-party licensing must be reviewed before adding new bundled components.
