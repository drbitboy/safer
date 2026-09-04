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

## `indi_xml_bridge.hpp/.cpp` — liblilxml API status

`decomposeVector()` uses the `liblilxml` C API (`XMLEle*`,
`findXMLAttValu`, `nXMLEle`, `nextXMLEle`, `pcdataXMLEle`). This was
originally written from general knowledge of libindi-family APIs with
every call marked `// VERIFY:`, since the actual MagAO-X headers
weren't reachable that session. Brian has since supplied the real
`lilxml.h` directly, and the code has been corrected against it:

- `findXMLAttValu`, `pcdataXMLEle`, `nXMLEle`: confirmed exact match.
- `nthXMLEle` **does not exist** in liblilxml — there's no
  index-based child accessor. The original draft's indexed loop was a
  bug; it's been rewritten to use the real (stateful) iterator,
  `nextXMLEle(ep, first)`.

Still worth checking before relying on this in production: whether
MagAO-X's fork of liblilxml differs from the upstream header Brian
supplied.

`recomposeVectorXml()` in the same file does **not** depend on any of
this — it's plain string templating and can be trusted as-is.

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
