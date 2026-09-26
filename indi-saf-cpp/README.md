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
  1b's not-yet-implemented connection to the local `indiserver`. Not
  used by `saf_local.cpp` (see below) — kept as illustration of the
  separate-thread alternative `example_1b_loop.cpp` demonstrates.
- `example_3a_loop.cpp` — illustrative outbound drain loop: drains the
  outbound `FileMailbox`, sends each record over `ILinkApi`, erases
  the file on accepted send.
- `example_1b_loop.cpp` — illustrative inbound drain loop, the mirror
  of the above: drains the inbound `FileMailbox`, calls
  `recomposeVectorXml()`, sends the result to `IIndiServerApi`, erases
  the file on success. (Not shown: whatever receives off the link and
  calls `inboundStore.upsert()` on each incoming message — that's the
  receiving counterpart to 3a's `ILinkApi::send` on the far side, and
  is out of scope for this bridge-logic package.) Superseded in
  practice by `saf_local.cpp` for the 1a+1b-combined design (see
  below) — kept as illustration of running 1b as its own separate
  thread/process instead, via the `IIndiServerApi` abstraction, if
  that shape is ever preferred over the combined loop.
- `saf_local_config.hpp/.cpp` + `saf_local.hpp/.cpp` — a REAL program
  (not illustrative), running 1a and 1b together as a single
  select()-paced thread sharing one TCP connection to `indiserver`
  (per the design discussion: 1a and 1b are both just "the INDI
  client" talking to the same local server, so there's a real benefit
  to combining them, not just simplicity). Talks to the socket
  directly rather than through `IIndiServerApi` — that interface
  existed to let `example_1b_loop.cpp` be written/tested against a
  swappable stub before the real connection mechanism was decided;
  `saf_local.cpp` **is** that real mechanism now, so the abstraction
  isn't needed here. Loop shape:

      (A)   check inbound mailbox for pending records
      (A.1) none pending -> (B)
      (A.2) pending -> recompose + send each over the socket, erase
            on success; EAGAIN or a hard send error stops draining
            for this lap rather than busy-looping
      (B)   select() on indiserver's read fd, with a timeout --
            this is what paces the loop; no sleep() anywhere
      (B.1) timeout -> back to (A)
      (B.2) readable -> recv(), feed the persistent LilXML parser,
            decompose any complete messages, upsert into the
            outbound mailbox -> back to (A)

  Deliberately no `select()` on the write side: write-readiness is
  essentially always true on a healthy connection, so it isn't the
  real gating condition — whether the inbound mailbox has anything
  pending is, and that's a plain directory check `FileMailbox`
  exposes directly, not something `select()` can watch.

  Config split into its own file (`saf_local_config.hpp/.cpp`)
  specifically so it has zero dependency on `liblilxml`/`mailbox.hpp`/
  `indi_xml_bridge.hpp` and can be linked and tested without a real
  `liblilxml` binary — see `test/saf_local_config_test.cpp` (8 cases,
  all passing: required-field validation, CLI flags, `--help`, bad
  input, INI file loading, CLI-overrides-config-file regardless of
  `--config`'s position on the command line, malformed/missing config
  files). `saf_local.cpp`'s `runLoop()` itself has its liblilxml
  parsing layer verified for real now too (see "Vendored
  liblilxml/base64 source" below) — what's specifically NOT
  runtime-tested is the socket/`select()` plumbing, since there's no
  running `indiserver` available to connect to in this environment.

  Command-line / INI parameters:

  | Flag | INI key | Default | Required |
  |---|---|---|---|
  | `-i, --inbound-dir` | `inbound_dir` | — | yes |
  | `-o, --outbound-dir` | `outbound_dir` | — | yes |
  | `-p, --indiserver-port` | `indiserver_port` | — | yes |
  | `-H, --indiserver-host` | `indiserver_host` | `127.0.0.1` | no |
  | `-t, --select-timeout-ms` | `select_timeout_ms` | `250` | no |
  | `-l, --mailbox-limit` | `mailbox_limit` | `100` | no |
  | `-r, --reconnect-delay-ms` | `reconnect_delay_ms` | `1000` | no |
  | `-v, --verbose` | `verbose` | `false` | no |
  | `-c, --config` | — | — | no |

  `--config`/`-c` loads an INI file as a base layer of settings;
  any flag actually given on the command line overrides the
  corresponding INI value, regardless of where `--config` appears
  among the other flags (tested explicitly — see Test 6).

  Parameters added beyond your original four, worth knowing about:
  `--indiserver-host` (you asked for a port; a "local" indiserver is
  still conventionally reached by address too, default `127.0.0.1`),
  `--mailbox-limit` (bounds how many records `peekPending()` drains
  per lap — protects against one huge backlog monopolizing the loop
  and starving `(B)`), `--reconnect-delay-ms` (indiserver connections
  will drop sometimes; this paces retry attempts rather than
  busy-reconnecting), and `--verbose` (stderr diagnostic logging,
  useful for exactly the kind of manual testing this file's own
  build/run instructions describe). Deliberately NOT added: anything
  daemonization-related (pidfile, fork-to-background) or TLS/auth for
  the indiserver connection — out of scope unless indiserver actually
  needs them.

  `runLoop()`'s previously-flagged `// VERIFY:` on `parseXMLChunk()`
  is now **confirmed correct**, not just assumed — see "Vendored
  liblilxml/base64 source" below.

## Vendored liblilxml/base64 source

`lilxml.c`/`.h` and `base64.c`/`.h` are copied directly from
`drbitboy/MagAOX`, branch `dev-resurrector`, `INDI/liblilxml/` — the
actual authoritative source this bridge targets, not just a header
someone pasted in. (Note: `dev-resurrector` specifically, not `dev` —
`dev`'s version of `lilxml.h` is missing `parseXMLChunk()` entirely,
which `saf_local.cpp` depends on; that turned out to matter.) Licenses
(as stated in each file's own header comment): `lilxml.c`/`.h` are
LGPL v2.1+; `base64.c` is GPL v2+ (no license header on `base64.h`
itself). Worth keeping in mind if `safer`'s own licensing needs to
account for that difference.

This `lilxml.h` is byte-for-byte identical to the one Brian uploaded
earlier in this project (the one `indi_xml_bridge.cpp` and
`saf_local.cpp` were checked against) — confirmed by `diff`. So the
API surface used throughout this bridge was already correct; what's
new here is that `lilxml.c`/`base64.c` (the actual implementations,
not just declarations) are now available to compile and link against,
which let the previously-`// VERIFY:`-tagged `parseXMLChunk()` usage
actually be exercised for real rather than left as an assumption.

`test/lilxml_integration_test.cpp` does this: builds a real `LilXML*`
context, feeds it actual XML text through `parseXMLChunk()`, and
confirms `decomposeVector()` correctly extracts the data. Three cases,
all passing:

1. A complete message in one chunk — one element back, values correct.
2. **The scenario this whole VERIFY flag was actually about**: the
   same message split across two separate `parseXMLChunk()` calls at
   an arbitrary mid-message byte. The first call returns zero complete
   elements (correctly incomplete); the second, fed the remainder,
   returns the fully-reassembled element — confirming the `LilXML*`
   context genuinely carries partial-parse state across calls the way
   `saf_local.cpp`'s `(B.2)` step depends on.
3. `recomposeVectorXml()`'s hand-written XML output round-trips back
   through the real parser correctly (confirms it produces genuinely
   valid, parseable INDI XML, not just plausible-looking text).

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
- `liblilxml` — now vendored in-tree (`lilxml.c`/`.h`, `base64.c`/`.h`
  — see "Vendored liblilxml/base64 source" above), so this is no
  longer an external dependency to separately provide. Compile/link
  `lilxml.c` and `base64.c` (which `lilxml.c` needs) alongside
  `indi_xml_bridge.cpp`/`saf_local.cpp` — see
  `test/lilxml_integration_test.cpp`'s build command for the exact
  invocation.

## Not yet implemented

- Whatever receives incoming wire messages off the link and calls
  `inboundStore.upsert()` on them (the receiving counterpart to 3a's
  `ILinkApi::send`) — 3a/the link side is still stubbed
  (`link_api.hpp`'s `StubLinkApi`); `saf_local.cpp` only covers 1a/1b
  (the indiserver-facing side).
- A real `ILinkApi` implementation (link transport mechanism still
  TBD).
- Runtime testing of `saf_local.cpp`'s `runLoop()` end-to-end —
  `test/lilxml_integration_test.cpp` now verifies the parsing layer
  for real (`parseXMLChunk()`, split-message reassembly,
  `recomposeVectorXml()` round-tripping), since `liblilxml` is
  vendored in-tree now. What's still unverified is specifically the
  socket/`select()` plumbing itself — no running `indiserver` is
  available in this environment to actually connect to.
- Anything BLOB-related (explicitly out of scope per the design doc).
