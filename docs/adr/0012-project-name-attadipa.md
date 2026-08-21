# ADR 0012: Project name is Attadipa

**Status:** accepted  
**Date:** 2026-08-21

## Decision

The platform formerly branded Firefly OS is now **Attadipa**. Lumar is its
firefly mascot. The name is expressed technically through **Independent by
design**: core functionality that can reasonably execute on-device must not
depend on a phone, cloud service, or persistent Internet connection merely for
architectural convenience.

All active public technical identifiers use `attadipa` / `ATTADIPA_`; no
compatibility aliases for the previous identifier are retained. Git history is
not rewritten, so historical references remain intact.

## Rationale

Attadipa names the whole wearable platform rather than only an operating
system. Its Pali origin, *attadīpa*, communicates self-reliance and matches the
existing architecture: local providers are preferred where practical, while a
phone, cloud, or Attadipa Node may enhance capability without becoming the
device's required brain.

## Consequences

- Public includes, namespaces, CMake targets, simulator binary, artifacts, and
  web storage keys use the new identifier.
- Existing persisted or published protocol identifiers require an explicit
  migration decision before changing. None exist in the current host-first
  implementation.
- Repository history and any explicit historical migration note may retain the
  former name.
