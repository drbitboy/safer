# INDI Store-and-Forward — Starter C++ Implementation

Corresponds to `indi-store-and-forward-design.md` decisions as of this session.

**noSQL branch**: the SQLite-backed, outbound-only `OutboundStore` has
been replaced with `FileMailbox` (`mailbox.hpp/.cpp`) — a file-backed,
directionless store used on **both** sides now. `spool.hpp/.cpp` (the
old inbound-only file spool) has been removed; `FileMailbox` supersedes
it. `wire_format.hpp/.cpp` gained a `msg_type` field (`"def"|"set"|"new"`)
to support the file-naming key, and `indi_xml_bridge.cpp`'s
`recomposeVectorXml()` now derives `def`/`set`/`new` from that field
directly instead of a separate bool.

## What's implemented and self-contained (no unverified dependencies)

- `mailbox.hpp/.cpp` — file-backed mailbox, one instance per direction
  (an outbound instance and a separate inbound instance). One
  `.init`→`.ready` file per `(msg_type, device, property, element)`
  key in that instance's directory; same-key writes overwrite the
  pending file in place for latest-value-wins, exactly mirroring what
  SQLite's `ON CONFLICT DO UPDATE` gave the earlier, outbound-only
  version. See the header for the full filename scheme and the
  confirmed design choice: `msg_type` IS part of the key (a pending
  `def` and a pending `set` for the same element are two separate
  files), and cross-key delivery order is not preserved or required —
  only same-key latest-value-wins matters.

  `peekPending()` uses a two-phase claim-then-glob scheme:
  `*.ready`→`*.sending` (atomic rename, claims ownership) then globs
  `*.sending` independently. This makes `erase()` race-free against a
  concurrent `upsert()` — `upsert()` never touches a `*.sending` file
  — and gives crash recovery for free with no reaper/timeout: a
  leftover `*.sending` from a reader that died mid-handling is either
  retried untouched (no newer `*.ready` arrived) or correctly
  superseded (a newer `*.ready` overwrote it, which is
  latest-value-wins doing its job, not data loss). See
  `test/mailbox_smoketest.cpp` for a runnable check of both cases,
  plus same-key overwrite and the msg_type-is-part-of-the-key
  behavior.
- `wire_format.hpp/.cpp` — text wire format, isolated in its own
  function per your instruction, so it can be swapped for something
  more compact later without touching 3a, 1b, or `FileMailbox`. Also
  doubles as the on-disk content of each `FileMailbox` pending file
  (both directions), and carries the `msg_type` field.
- `link_api.hpp` — stubbed `ILinkApi` interface with a trivial
  `StubLinkApi` for local dev/testing, per your instruction to stub
  the link API out. Outbound (3a) side only.
- `indiserver_api.hpp` — stubbed `IIndiServerApi` interface with a
  trivial `StubIndiServerApi`, mirroring `ILinkApi`'s pattern, for
  1b's not-yet-implemented connection to the local `indiserver`.
- `example_3a_loop.cpp` — illustrative outbound drain loop: drains the
  outbound `FileMailbox`, sends each record over `ILinkApi`, erases
  the file on accepted send.
- `example_1b_loop.cpp` — illustrative inbound drain loop, the mirror
  of the above: drains the inbound `FileMailbox`, calls
  `recomposeVectorXml()`, sends the result to `IIndiServerApi`, erases
  the file on success. (Not shown: whatever receives off the link and
  calls `inboundStore.upsert()` on each incoming message — that's the
  receiving counterpart to 3a's `ILinkApi::send` on the far side, and
  is out of scope for this bridge-logic package.)

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
this — it's plain string templating and can be trusted as-is. It now
takes only a `MailboxElement` (no separate bool) and derives the
`def`/`set`/`new` prefix from `el.msg_type`.

## Build dependencies

- C++17 (uses `std::optional`, `std::filesystem`) — the only
  dependency for `mailbox.cpp`/`wire_format.cpp` now that SQLite has
  been removed on this branch.
- MagAO-X's `liblilxml` fork (and whatever it depends on) — only for
  `indi_xml_bridge.cpp`'s `decomposeVector()`; API confirmed, see above.

## Not yet implemented

- 1a's and 1b's outer loops beyond what `example_3a_loop.cpp`/
  `example_1b_loop.cpp` illustrate: actually connecting to the local
  `indiserver` and reading its XML stream (1a), and actually
  connecting to the local `indiserver`'s socket/FIFO to write (1b, via
  a real `IIndiServerApi` implementation).
- Whatever receives incoming wire messages off the link and calls
  `inboundStore.upsert()` on them (the receiving counterpart to 3a's
  `ILinkApi::send`).
- Real `ILinkApi` and `IIndiServerApi` implementations (both
  explicitly stubbed — link API and indiserver connection mechanism
  both still TBD).
- Anything BLOB-related (explicitly out of scope per the design doc).
