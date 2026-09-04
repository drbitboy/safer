# CLAUDE.md

Context for future Claude sessions working in this repo.

## What this is

A store-and-forward bridge between two INDI servers — one ground, one
space — connected only during scheduled contact windows. Not a
general-purpose INDI proxy: it's a delay/disruption-tolerant sync layer
that keeps the "latest value" of every INDI property consistent on both
sides across blackouts.

Read `indi-store-and-forward-design.md` first. It has the full problem
statement, sync model, and architecture decisions log. Read
`indi-saf-cpp/README.md` for what's implemented vs. stubbed vs. unverified.

## Key facts to hold onto

- **Uniqueness key** for any INDI value: `(device, property name, element
  name)`. Not `label` — `label` defaults to `name` when absent and is
  descriptive metadata, not identity.
- **Latest-value-wins** sync model, symmetric in both directions. No
  command queue or transition history preserved across a blackout.
- Four-component pipeline per side:
  1. INDI-client thread — talks to the local `indiserver`, decomposes
     outbound XML into rows, recomposes inbound rows into XML.
  2. SQLite mailbox — stores latest value per key, both directions.
  3. Link-facing drain thread — moves rows to/from the link when it's up.
  4. (Eliminated) — inbound recompose is handled by the link itself, not a
     separate component.
- Inbound file-spool naming: UTC, one-second resolution, and each writer
  thread waits ≥1.1s after writing a file before writing/naming the next,
  to guarantee unambiguous ordering. Inbound and outbound use **separate
  directories** — no cross-thread coordination needed.
- BLOBs are explicitly out of scope for now.

## What's unverified

`indi_xml_bridge.cpp`/`.hpp` was originally written from general
knowledge of `liblilxml`/`libindi`-style interfaces, with every call
marked `// VERIFY:`. Brian has since supplied the real `lilxml.h`, and
the code has been corrected against it — `findXMLAttValu`,
`pcdataXMLEle`, and `nXMLEle` all matched; `nthXMLEle` did not exist
(no index-based child accessor in liblilxml) and has been replaced
with the real stateful iterator, `nextXMLEle(ep, first)`. See
`indi-saf-cpp/README.md` for the full status.

Still open: whether MagAO-X's own fork of liblilxml (the design
conversation referenced `github.com/magao-x/MagAOX`, subdirectory
`INDI/liblilxml/`) differs from the upstream header Brian supplied.

## Conventions

- This is spacecraft operations tooling — precision matters more than
  usual. When uncertain whether something is verified against the spec
  vs. inferred, say so explicitly rather than asserting it.
- The INDI protocol spec referenced throughout is v1.7 (document v1.3),
  fetched from `docs.indilib.org/protocol/INDI.pdf`.
