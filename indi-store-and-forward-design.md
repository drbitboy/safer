# INDI Store-and-Forward Bridge — Design Notes

**Author context:** Brian T. Carcich, NASA / Ascending Node Technologies
**Purpose:** Bridge two INDI servers — one on the ground, one in space — over a
link that is only available during scheduled contact windows.

---

## 1. Problem Statement

- Two INDI servers on separate hosts: one **ground**, one **space**.
- The link between them is **not continuous** — it is only up during contact
  windows (e.g., ~1 hour/day, or several ~20-minute passes/day).
- This is a scheduled-connectivity problem, closer to delay/disruption-tolerant
  networking (DTN) than to a flaky-but-usually-up link.
- Traffic is **bi-directional**: INDI parameters (properties) flow both
  ground→space and space→ground.
- Scale: **thousands of properties** on each side. Full resync every contact
  window is not viable — delta/dirty tracking is required.
- BLOB handling is explicitly **out of scope for now** — focus is on ordinary
  INDI parameters (Text/Number/Switch/Light).

## 2. Sync Model

- **Latest-value-wins.** No command queue, no full transaction history is
  preserved across a blackout — only the newest value of each parameter
  matters once the link comes back up.
- Applies symmetrically in both directions.
- Property `state` (Idle/Ok/Busy/Alert) travels with the value as part of the
  same "latest" unit — no separate transition history is kept.

## 3. INDI Structural Facts (from the protocol spec, v1.7 / doc v1.3)

- Hierarchy is exactly three levels: **Device → Property (vector) → Element**.
- **Uniqueness key** for a single scalar value:
  **`(device, property name, element name)`**
  - `device` + `name` (property name) uniquely identify a *vector*
    (e.g. `defNumberVector`, `setSwitchVector`, etc.)
  - Each element inside a vector (`defNumber`, `oneSwitch`, etc.) has its own
    `name` attribute, unique *within that vector* — this is the element name.
  - Both levels are literally called `name` in the XML, at different nesting
    levels — easy to conflate, must not be.
- Everything else on a vector (`label`, `group`, `state`, `perm`, `timeout`,
  `timestamp`, `message`, plus type-specific `format`/`min`/`max`/`step`/`rule`)
  is descriptive metadata, not part of the identity key.
- INDI explicitly permits **partial vector updates** — a `setXXXVector` only
  needs to include the elements that changed. This is what makes
  element-level dirty-tracking both valid and efficient.
- IANA-assigned INDI port: TCP **7624** (only relevant if ever running raw
  INDI directly over a socket transport).

## 4. Architecture — Three Stages Per Host

Each host (ground and space) runs the same stack, mirrored:

```
 local indiserver                                        the link
        |                                                    |
   [1a] INDI client (outbound)                          [3a] link-facing
   - reads local server's                                     (outbound)
     setXXX/defXXX XML                                   - drains dirty rows
   - decomposes vector into                                from SQLite when
     per-element records                                   link is up
   - UPSERT into SQLite                                  - sends compact
     keyed (device, property,                              per-element wire
     element)                                               messages
        |                                                  - deletes row from
        v                                                    SQLite on
   [ SQLite DB ]  <---------------------------------------- accepted send
   outbound-only mailbox
   (dirty/pending changes
   waiting to cross the link)

   [1b] INDI client (inbound)
   - watches an INBOUND SPOOL
     DIRECTORY (plain files)
   - wraps each file's contents
     back into valid vector XML
     (device= / name= wrapper,
     one or more oneXXX children)
   - sends to local indiserver
   - unlinks file on success
        ^
        |
   inbound spool directory  <---------------------------- [ the link itself ]
   (files written directly by                              handles inbound
   the link — no separate 3b                                receipt and
   component; the link IS the                               writes spool
   inbound writer)                                           files directly
```

### Component roles

1. **1a — INDI client, outbound direction**
   Connects to the local `indiserver`. Receives outbound `setXXX`/`defXXX`
   messages. Decomposes each vector into per-element records. UPSERTs each
   into the SQLite table. This is the **only** place decomposition happens —
   doing it here (at the true source of a change) means the wire format sent
   by 3a can be compact (per-element), saving link bandwidth, without ever
   needing to re-derive structure downstream.

2. **SQLite DB — outbound mailbox only**
   Holds pending (not-yet-sent) parameter changes, keyed by
   `(device, property, element)`. UPSERT semantics: every write does
   `INSERT ... ON CONFLICT(device, property, element) DO UPDATE ...`, which
   naturally gives latest-value-wins and coalesces any number of intermediate
   changes during a blackout into a single row.
   Inbound direction does **not** use SQLite at all (see below).

3. **3a — link-facing, outbound direction**
   When the link is up, drains pending rows from SQLite, transmits a compact
   per-element wire message for each, and — per current working assumption —
   **deletes the row once the link accepts the send**. This assumption
   depends on the (currently TBD) link-send API actually guaranteeing durable
   delivery, not just local hand-off; flagged as something to revisit once
   that API is defined (see Open Questions).

4. **Inbound path — no SQLite, file-spool based instead**
   The link's inbound handling **is** the writer of inbound files — there is
   no separate 3b component; the link itself writes each received message to
   a file in the inbound spool directory. This works because the *outbound*
   side has already fully deduplicated/coalesced via SQLite before
   transmission, so nothing arriving inbound needs further dedup — it's
   already guaranteed to be the latest value for that key. This makes the
   inbound path a simple durable pass-through:
   link → file (persists it) → 1b reads, recomposes to valid INDI XML, sends
   to local server → unlink file on success.

5. **1b — INDI client, inbound direction**
   Watches the inbound spool directory. For each file, wraps its contents
   into a valid INDI vector element (`device=`, `name=` attributes, one or
   more `oneXXX` children — even a single-element vector is valid per spec,
   since partial vectors are allowed). Sends to local `indiserver`. Unlinks
   the file once successfully sent — this is the durability/retry mechanism:
   if the connection to the local server is down, the file simply stays and
   is retried.

## 5. File-Naming / Ordering Scheme (Inbound & Outbound Spool Directories)

- **Two separate spool directories** — inbound and outbound — never shared,
  so no cross-thread coordination is needed.
- Filenames use **UTC timestamps at one-second resolution**.
- Any thread writing into a spool directory **waits at least 1.1s** after
  writing one file before writing/naming the next, guaranteeing strict
  ordering by filename within that directory.
- Only two threads ever write filenames that need this ordering guarantee,
  and each owns its own directory exclusively:
  - the link's inbound-writing logic → inbound spool directory
  - (previously proposed 3a-writes-outbound-files idea was superseded —
    3a now writes directly to the link, not to a spool file, since outbound
    durability is handled by SQLite instead)

## 6. Decisions Log

| # | Topic | Decision |
|---|-------|----------|
| 1 | Where to decompose | Only on the **outbound** side (in 1a), upstream of the UPSERT, to preserve the `(device, property, element)` key and to save link bandwidth by sending compact per-element wire messages. |
| 2 | DB write semantics | **UPSERT** (`INSERT ... ON CONFLICT DO UPDATE`), not append — gives latest-value-wins and natural coalescing. |
| 3 | Concurrency (SQLite, two threads) | **Deferred** — may resolve itself. `WAL` mode noted as a cheap default worth enabling regardless. |
| 4 | Outbound row lifecycle | Row is **deleted once the link accepts the send** — working assumption, pending definition of the actual outbound link-send API. Flagged for revisit: "accepted" needs to mean durably delivered to the peer, not just handed to the transmitter, or messages could be lost mid-pass. |
| 5 | Inbound path storage | **No SQLite for inbound** — inbound messages are already deduplicated at the true source (outbound side's SQLite), so the inbound side only needs a durable pass-through, implemented as a **file spool directory**, not a database. |
| 6 | Spool directories | **Separate inbound and outbound directories** to avoid needing cross-thread filename coordination. |
| 7 | 3b component | **Eliminated** — the link's own inbound-handling logic writes spool files directly; a separate "3b" stage is unnecessary. |

## 7. Open Questions / To Revisit

- **Link send-API semantics (affects Decision #4):** Once the outbound
  link-transmission API is defined, confirm whether "accepted" means
  durable delivery to the peer's spool, or merely local hand-off to a
  radio/modem buffer. If only the latter, deleting the SQLite row on send
  is unsafe — may need an application-level ack from the peer instead of
  trusting the link API alone.
- **SQLite concurrency** between 1a (writer) and 3a (reader/deleter) —
  deferred for now; revisit if it doesn't resolve itself naturally.
  `PRAGMA journal_mode=WAL;` flagged as a cheap default to enable regardless.
- **Compact wire message format** for 3a's outbound transmissions — not yet
  designed. Needs to carry at minimum: device, property, element, value,
  and enough state/metadata for 1b to reconstruct a valid vector wrapper.
- **BLOB handling** — explicitly deferred; will need separate treatment
  given INDI's `enableBLOB` flow-control semantics and the spec's warnings
  about servers not blocking on slow BLOB transfers.

## 8. Reference

- INDI protocol white paper: Protocol Version 1.7, Document Version 1.3,
  18 June 2007, © 2003–2007 Elwood Charles Downey.
  Retrieved from `http://docs.indilib.org/protocol/INDI.pdf`.
  (Note: `clearskyinstitute.com/INDI/INDI.pdf`, often cited as the canonical
  source, blocks automated/bot fetching via robots.txt and was not directly
  read for this discussion.)
