# safer

Store-and-forward bridge between two INDI servers (ground and space) over a
link that is only available during scheduled contact windows.

## Contents

- [`indi-store-and-forward-design.md`](indi-store-and-forward-design.md) —
  design notes: problem statement, sync model, INDI protocol structural
  facts, the 4-component pipeline architecture, file-naming scheme, and open
  questions.
- [`indi-saf-cpp/`](indi-saf-cpp/) — starter C++ implementation:
  - `outbound_store.hpp/.cpp` — SQLite-backed mailbox, UPSERT-keyed on
    `(device, property, element)`, WAL mode.
  - `wire_format.hpp/.cpp` — text wire-format encode/decode, isolated behind
    its own functions so a more compact format can be swapped in later.
  - `link_api.hpp` — stubbed link send/receive interface (`ILinkApi`).
  - `spool.hpp/.cpp` — inbound file-spool writer/reader (UTC filenames,
    1.1s minimum spacing to guarantee lexical/temporal ordering).
  - `indi_xml_bridge.hpp/.cpp` — decompose INDI XML vectors into per-element
    rows and recompose rows back into INDI XML.
  - `example_3a_loop.cpp` — illustrative drain loop tying the pieces
    together.
  - See `indi-saf-cpp/README.md` for what's verified against the INDI spec
    vs. still assumed/unverified.
- `conversation-transcript.md` — transcript of the design conversation that
  produced this code, kept alongside it in case anything needs to be
  retraced.

## Status

Early-stage starter package: the SQLite mailbox, wire format, and file-spool
pieces are implemented; the actual link transport is a stub
(`StubLinkApi`); the INDI XML parsing side is marked with `// VERIFY:`
comments where it was written from general knowledge of `liblilxml`-style
APIs rather than confirmed against a specific INDI library header.

See `indi-store-and-forward-design.md` for open questions and next steps.
