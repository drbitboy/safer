# INDI Store-and-Forward — Starter C++ Implementation

Corresponds to `indi-store-and-forward-design.md` decisions as of this session.

## What's implemented and self-contained (no unverified dependencies)

- `outbound_store.hpp/.cpp` — SQLite outbound mailbox. UPSERT-keyed on
  `(device, property, element)`. Uses the raw `sqlite3` C API directly
  (my choice, per your "you choose"). WAL mode enabled as a cheap
  default per decision #3, even though full concurrency handling
  between the 1a writer and 3a reader/deleter threads is deferred.
- `wire_format.hpp/.cpp` — text wire format, isolated in its own
  function per your instruction, so it can be swapped for something
  more compact later without touching 3a, 1b, or the DB schema.
- `link_api.hpp` — stubbed `ILinkApi` interface with a trivial
  `StubLinkApi` for local dev/testing, per your instruction to stub
  the link API out.
- `spool.hpp/.cpp` — inbound file-spool writer/reader with the UTC
  1-second-resolution + 1.1s-minimum-wait ordering scheme, per
  decisions #5–7 (no SQLite on the inbound path; separate directories;
  no 3b component — the link itself is the inbound writer).
- `example_3a_loop.cpp` — illustrative drain loop tying the above
  together.

## What depends on an UNVERIFIED API — check before compiling

- `indi_xml_bridge.hpp/.cpp` — `decomposeVector()` assumes the
  standard `liblilxml`/`libindi`-family C API (`XMLEle*`,
  `findXMLAttValu`, `nXMLEle`, `nthXMLEle`, `pcdataXMLEle`, etc.).
  **I was not able to browse the actual file contents in
  `magao-x/MagAOX/INDI/liblilxml/` this session** — GitHub's
  file/raw-content pages didn't come back through search or fetch in
  a form I could read. Every call is marked `// VERIFY:` in the code.
  Check function names, signatures, and header paths against the real
  MagAO-X source before compiling this file.
- `recomposeVectorXml()` in the same file does **not** have this
  dependency — it's plain string templating and can be trusted as-is.

## Build dependencies

- `libsqlite3` (dev headers) — for `outbound_store.cpp`.
- C++17 (uses `std::optional`, `std::filesystem`).
- The real MagAO-X `liblilxml` (and whatever it depends on) — only for
  `indi_xml_bridge.cpp`'s `decomposeVector()`, once verified.

## Not yet implemented

- 1a and 1b's outer loops (connecting to the local `indiserver`,
  reading its XML stream, calling `decomposeVector`/`OutboundStore::upsert`
  or `SpoolReader::pollOnce`/`recomposeVectorXml` respectively, and
  actually writing to the indiserver socket/FIFO).
- Real `ILinkApi` implementation (explicitly stubbed per your
  instruction — link API itself still TBD).
- Anything BLOB-related (explicitly out of scope per the design doc).
