---
status: accepted
---

# Use the official 3.2.1 release as the ARM client baseline

The existing Pandora-derived fork contains Zandronum 3.1-alpha and does not establish a working AArch64 client. We will start from the exact official 3.2.1 release and selectively port reviewed ARM and audio fixes, rather than upgrading the legacy engine forward. This preserves a clear gameplay and stock-server compatibility baseline while avoiding reconstruction of the intervening engine changes.

The authoritative release is Mercurial `ZA_3.2.1` at `a701547f4162dd965c25f082a55be2787310e6ea`, mapped to Git `28f736fb3f77bf0b767ec5468191bc05d0fccbb9`. Release protocol identity must be preserved independently of the port's later commits; an actual join and gameplay test against an unmodified official server is required. See the [release research](../research/upstream-3.2.1.md) and [legacy fork audit](../research/current-arm-fork.md).

This decision selects the source baseline. The user confirmed the completed [design interview](../arm-port-design.md) on 2026-10-14; repository-history migration and implementation are subsequent work.
