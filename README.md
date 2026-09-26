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
  differs from `develop`). Directory layout: `lib/` (our own .cpp
  files), `include/` (our own .hpp/.h files), `app/` (files with a
  `main()` — currently just `saf_local.cpp`),
  `third_party/liblilxml/` (vendored liblilxml/base64 source). Build
  with `cd indi-saf-cpp && make` (builds `saf_local`) or `make test`
  (runs everything in `test/`). Key pieces:
  - `mailbox.hpp/.cpp` — file-backed mailbox (`FileMailbox`), used
    symmetrically for both outbound and inbound directions, keyed on
    `(msg_type, device, property, element)`. No SQLite dependency.
  - `wire_format.hpp/.cpp` — text wire-format encode/decode, isolated behind
    its own functions so a more compact format can be swapped in later;
    also the on-disk content format for `FileMailbox` pending files.
  - `link_api.hpp` — stubbed outbound link send/receive interface (`ILinkApi`).
  - `indiserver_api.hpp` — stubbed inbound local-indiserver send interface
    (`IIndiServerApi`), not used by `saf_local` (see below).
  - `indi_xml_bridge.hpp/.cpp` — decompose INDI XML vectors into per-element
    rows and recompose rows back into INDI XML.
  - `example_3a_loop.cpp` / `example_1b_loop.cpp` — illustrative outbound
    and inbound drain loops tying the pieces together.
  - `saf_local_config.hpp/.cpp` + `saf_local.hpp/.cpp` — the REAL
    program: runs 1a+1b together as a single select()-paced thread
    with one TCP connection to `indiserver`. Builds to a genuine,
    linked, runnable `saf_local` binary.
  - `third_party/liblilxml/lilxml.c/.h`, `base64.c/.h` — vendored
    verbatim from `drbitboy/MagAOX`'s `dev-resurrector` branch
    (LGPL v2.1+ / GPL v2+ respectively).
  - See `indi-saf-cpp/README.md` for what's verified against the INDI spec
    vs. still assumed/unverified.
- `conversation-transcript.md` — transcript of the design conversation that
  produced this code, kept alongside it in case anything needs to be
  retraced.

## Status

Early-stage starter package: the file-backed mailbox, wire format, and
XML bridge pieces are implemented and used symmetrically on both
sides. `saf_local` is a real, working, linked binary (1a+1b combined
into a single select()-paced thread) — `./saf_local --help` works;
its liblilxml parsing layer is genuinely tested end-to-end, but its
socket/`select()` plumbing hasn't been runtime-tested against an
actual `indiserver`. The outbound link transport is still a stub
(`StubLinkApi`); the INDI XML parsing side has been checked against
MagAO-X's own `lilxml.c`/`.h`, now vendored in-tree rather than just a
header reference (see `indi-saf-cpp/README.md`).

See `indi-store-and-forward-design.md` for open questions and next steps.
