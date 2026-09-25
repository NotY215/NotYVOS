# ADR 0008 — Reserved domains not created as empty directories

Status: Accepted

## Decision
Reserved subsystems (`runtime/ps3/`, `graphics/`, `drivers/`, `security/`,
`recovery/`, `apps/`, `overlay/`, `win_compat/`) are documented here but
not created as empty trees. They are created when their phase begins.