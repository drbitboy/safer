# saf-scripts

Helper scripts for the safer store-and-forward bridge (siblings of `indi-saf-cpp/`).

## saf_link_ssh_prototype.bash

PROTOTYPE link sender. Moves the local outbound archive
`<link-dir>/sending.zip` (built by `saf_local`'s outbound archiver) to a
remote inbound directory over rsync + ssh, deleting the local copy once the
transfer is confirmed.

```
saf_link_ssh_prototype.bash -l LOCAL_LINK_DIR -r HOST:REMOTE_INBOUND_DIR/ \
                            [-i SECONDS] [-1] [-v] [--disable-fsync]
```

| Option | Meaning | Default |
|---|---|---|
| `-l DIR` | local outbound link directory (e.g. `./outbound/link`) | required |
| `-r HOST:PATH/` | remote host and the remote *inbound link* directory (e.g. `user@host:/var/saf/inbound/link/`); `sending.zip` lands directly in it, where `saf_local`'s unpacker (`--inbound-link-dir`) picks it up | required |
| `-i N` | poll interval, seconds | `5` |
| `-1` | one pass, then exit (exit codes below) | loop forever |
| `-v` | log the decision made on every pass | off |
| `--disable-fsync` | do not pass `--fsync` to rsync | fsync on |

Behavior per pass:

1. Nothing happens unless `<link-dir>/link.is.active` exists (the link-up
   gate; whatever decides the link is up creates/removes it).
2. Nothing happens unless `<link-dir>/sending.zip` exists.
3. `rsync --ignore-existing --fsync` sends it to `HOST:PATH/sending.zip`.
   rsync writes a hidden temp file (`.sending.zip.XXXXXX`) in the remote
   directory and renames it into place only when complete, so a partial
   `sending.zip` is never visible. `--ignore-existing` means an unconsumed
   remote `sending.zip` is never overwritten.
4. `--ignore-existing` makes rsync exit 0 even when it skips the file, so the
   local file is deleted only if rsync exited 0 **and** itemized the file as
   newly created. Otherwise it is kept and retried next pass.

"Acknowledged" means the file arrived intact on the remote filesystem. The
remote consumer must delete the remote `sending.zip` after unpacking it; that
is what lets the next archive through. A lost acknowledgment resolves itself:
the local copy is resent once the remote one is gone (a harmless duplicate).

Exit codes with `-1`: `0` delivered or nothing to do, `1` transfer failed,
`2` usage/startup error (including another instance holding the lock),
`3` deferred because the remote still holds an unconsumed `sending.zip`.
Loop mode exits `0` on INT/TERM after finishing any transfer in progress.

Needs rsync >= 3.2.4 (for `--fsync`; checked at startup) and `flock`.
`SAF_RSYNC` and `SAF_SSH` override the rsync/ssh commands (used by the tests).

### Tests

`bash saf-scripts/test/test_saf_link_ssh_prototype.bash` (16 checks). Uses the
real rsync with an ssh stand-in that runs the remote command locally, so no
second machine or sshd is needed. The real ssh transport is therefore not
exercised by the tests.
