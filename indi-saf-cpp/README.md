# INDI Store-and-Forward — Starter C++ Implementation

Corresponds to `indi-store-and-forward-design.md` decisions as of this session.

**noSQL branch**: `outbound_store.hpp/.cpp` has been rewritten to use
plain files instead of SQLite — see its header comment for the full
scheme. `wire_format.hpp/.cpp` gained a `msg_type` field
(`"def"|"set"|"new"`) to support the new file-naming key. Everything
else is unchanged from `develop`.

## What's implemented and self-contained (no unverified dependencies)

- `outbound_store.hpp/.cpp` — file-backed outbound mailbox (noSQL
  branch — no SQLite dependency). One `.init`→`.ready` file per
  `(msg_type, device, property, element)` key in the outbound
  directory; same-key writes overwrite the pending file in place for
  latest-value-wins, exactly mirroring what SQLite's
  `ON CONFLICT DO UPDATE` gave the earlier version. See the header for
  the full filename scheme and the accepted tradeoff (msg_type is now
  part of the key, unlike the SQLite version's `(device, property,
  element)`-only primary key).
- `wire_format.hpp/.cpp` — text wire format, isolated in its own
  function per your instruction, so it can be swapped for something
  more compact later without touching 3a, 1b, or `OutboundStore`. Now
  also doubles as the on-disk content of each `OutboundStore` pending
  file, and carries the new `msg_type` field.
- `link_api.hpp` — stubbed `ILinkApi` interface with a trivial
  `StubLinkApi` for local dev/testing, per your instruction to stub
  the link API out.
- `spool.hpp/.cpp` — inbound file-spool writer/reader with the UTC
  1-second-resolution + 1.1s-minimum-wait ordering scheme, per
  decisions #5–7 (separate directories; no 3b component — the link
  itself is the inbound writer). Distinct from `OutboundStore`'s
  keyed-overwrite scheme — see spool.hpp's header comment.
- `example_3a_loop.cpp` — illustrative drain loop tying the above
  together; updated for `OutboundStore::peekPending()`'s new
  `PendingRecord` return type (parsed element + the exact filepath to
  pass to `erase()`).

## `indi_xml_bridge.hpp/.cpp` — liblilxml API status

`decomposeVector()` uses the `liblilxml` C API (`XMLEle*`,
`findXMLAttValu`, `nXMLEle`, `nextXMLEle`, `pcdataXMLEle`). This was
originally written from general knowledge of libindi-family APIs with
every call marked `// VERIFY:`, since the actual MagAO-X headers
weren't reachable that session. Brian has since supplied MagAO-X's own
fork of `lilxml.h` directly, and the code has been corrected against
it:

- `findXMLAttValu`, `pcdataXMLEle`, `nXMLEle`: confirmed exact match.
- `nthXMLEle` **does not exist** in liblilxml — there's no
  index-based child accessor. The original draft's indexed loop was a
  bug; it's been rewritten to use the real (stateful) iterator,
  `nextXMLEle(ep, first)`.

This API surface is now fully confirmed against the actual fork this
bridge targets — no remaining open question on that front.

`recomposeVectorXml()` in the same file does **not** depend on any of
this — it's plain string templating and can be trusted as-is.

## Build dependencies

- C++17 (uses `std::optional`, `std::filesystem`) — the only
  dependency for `outbound_store.cpp`/`wire_format.cpp` now that
  SQLite has been removed on this branch.
- MagAO-X's `liblilxml` fork (and whatever it depends on) — only for
  `indi_xml_bridge.cpp`'s `decomposeVector()`; API confirmed, see above.

## Not yet implemented

- 1a and 1b's outer loops (connecting to the local `indiserver`,
  reading its XML stream, calling `decomposeVector`/`OutboundStore::upsert`
  or `SpoolReader::pollOnce`/`recomposeVectorXml` respectively, and
  actually writing to the indiserver socket/FIFO).
- Real `ILinkApi` implementation (explicitly stubbed per your
  instruction — link API itself still TBD).
- Anything BLOB-related (explicitly out of scope per the design doc).
