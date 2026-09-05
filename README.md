# safer

Store-and-forward bridge between two INDI servers (ground and space) over a
link that is only available during scheduled contact windows.

## Contents

- [`indi-store-and-forward-design.md`](indi-store-and-forward-design.md) —
  design notes: problem statement, sync model, INDI protocol structural
  facts, the 4-component pipeline architecture, file-naming scheme, and open
  questions.
- [`indi-saf-cpp/`](indi-saf-cpp/) — starter C++ implementation (this
  branch, `noSQL`; see `indi-saf-cpp/README.md`'s top note for how it
  differs from `develop`):
  - `mailbox.hpp/.cpp` — file-backed mailbox (`FileMailbox`), used
    symmetrically for both outbound and inbound directions, keyed on
    `(msg_type, device, property, element)`. No SQLite dependency.
  - `wire_format.hpp/.cpp` — text wire-format encode/decode, isolated behind
    its own functions so a more compact format can be swapped in later;
    also the on-disk content format for `FileMailbox` pending files.
  - `link_api.hpp` — stubbed outbound link send/receive interface (`ILinkApi`).
  - `indiserver_api.hpp` — stubbed inbound local-indiserver send interface
    (`IIndiServerApi`).
  - `indi_xml_bridge.hpp/.cpp` — decompose INDI XML vectors into per-element
    rows and recompose rows back into INDI XML.
  - `example_3a_loop.cpp` / `example_1b_loop.cpp` — illustrative outbound
    and inbound drain loops tying the pieces together.
  - See `indi-saf-cpp/README.md` for what's verified against the INDI spec
    vs. still assumed/unverified.
- `conversation-transcript.md` — transcript of the design conversation that
  produced this code, kept alongside it in case anything needs to be
  retraced.

## Status

Early-stage starter package: the file-backed mailbox, wire format, and
XML bridge pieces are implemented and used symmetrically on both
sides; the actual link transport and indiserver connection are both
stubs (`StubLinkApi`, `StubIndiServerApi`); the INDI XML parsing side
has been checked against MagAO-X's own `lilxml.h` (see
`indi-saf-cpp/README.md`).

See `indi-store-and-forward-design.md` for open questions and next steps.
