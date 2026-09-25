# ADR 0006 — No Sony keys, no decryption bypass

Status: Accepted

## Decision
NOTYVOS contains no Sony private keys, no decryption keys, no bypass
mechanisms, and no extraction tooling. Users provide a legally prepared
firmware module. The runtime fails loudly if it is missing.