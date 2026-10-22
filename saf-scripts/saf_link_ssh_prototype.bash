#!/usr/bin/env bash
# saf_link_ssh_prototype.bash
#
# PROTOTYPE link sender for the safer store-and-forward bridge: moves the
# local outbound archive <link-dir>/sending.zip (made by saf_local's
# outbound archiver) to the remote inbound directory over rsync + ssh,
# then deletes the local copy once rsync confirms the transfer.
#
# Usage:
#   saf_link_ssh_prototype.bash -l LOCAL_LINK_DIR -r HOST:REMOTE_INBOUND_DIR/
#                               [-i SECONDS] [-1] [-v] [--disable-fsync]
#
#   -l DIR   local outbound link directory (the one holding sending.zip
#            and the link.is.active gate file)
#   -r H:P   remote host and remote inbound LINK directory, e.g.
#            user@groundhost:/var/saf/inbound/link/ ; sending.zip lands
#            directly in that directory (a trailing / is added if missing)
#   -i N     poll interval in seconds (default 5)
#   -1       run a single pass and exit (see exit codes below)
#   -v       verbose: log each rsync attempt, and (once each) when
#            link.is.active is detected and when it is no longer detected
#   --disable-fsync
#            do NOT pass --fsync to rsync (default: fsync the received
#            file; needs rsync >= 3.2.4 on both ends)
#
# One pass:
#   1. Do nothing unless <link-dir>/link.is.active exists.
#   2. Do nothing unless <link-dir>/sending.zip exists.
#   3. rsync --ignore-existing sending.zip to HOST:PATH/sending.zip.
#      rsync writes a hidden temp file in the remote directory and
#      renames it to sending.zip only when the transfer is complete, so
#      the remote never sees a partial sending.zip. --ignore-existing
#      means a remote sending.zip that has not yet been consumed is
#      NEVER overwritten.
#   4. --ignore-existing makes rsync exit 0 even when it skipped the
#      file, so success is decided from rsync's --out-format output: the
#      local sending.zip is deleted ONLY if rsync exited 0 AND reported
#      creating the remote sending.zip. In every other case the local
#      file is kept and retried on the next pass.
#
# "Acknowledged" here means the file arrived intact on the remote
# filesystem (rsync verifies a whole-file checksum). It does not mean the
# remote side has unpacked it; the remote consumer must delete the remote
# sending.zip after extracting it, which is what allows the next one in.
# A lost acknowledgment resolves itself: the local copy is resent once
# the remote consumer has deleted the earlier delivery (a harmless
# duplicate: applying the same values twice changes nothing).
#
# Exit codes (-1 mode; loop mode exits 0 on INT/TERM):
#   0 delivered, or nothing to do   1 transfer failed
#   2 usage / startup error         3 deferred: remote still holds an
#                                     unconsumed sending.zip
#
# Environment overrides (mainly for testing):
#   SAF_RSYNC   rsync command (default: rsync)
#   SAF_SSH     ssh command   (default: ssh)

set -u -o pipefail

readonly GATE_FILE="link.is.active"
readonly ARCHIVE="sending.zip"

LINK_DIR=""
REMOTE=""
INTERVAL=5
ONCE=0
VERBOSE=0
FSYNC=1
STOP=0
LAST_MSG=""
ERRFILE=""
SLEEP_PID=""
GATE_SEEN=0   # "gate seen" state for transition logging; set properly at
              # startup, once -l is known (see below)

RSYNC_CMD="${SAF_RSYNC:-rsync}"
SSH_CMD="${SAF_SSH:-ssh}"

usage() {
    sed -n '2,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' >&2
}

log() {
    printf '%s saf_link: %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$*" >&2
}

vlog() {
    (( VERBOSE )) && log "$*"
    return 0
}

# Log a state message only when it differs from the previous one (or in
# -v mode), so a long wait does not repeat the same line every poll.
log_change() {
    if [[ "$1" != "$LAST_MSG" ]] || (( VERBOSE )); then
        log "$1"
    fi
    LAST_MSG="$1"
}

die() {
    log "ERROR: $*"
    exit 2
}

cleanup() {
    [[ -n "$ERRFILE" ]] && rm -f -- "$ERRFILE"
    [[ -n "$SLEEP_PID" ]] && kill "$SLEEP_PID" 2>/dev/null
    return 0
}

on_signal() {
    STOP=1
    [[ -n "$SLEEP_PID" ]] && kill "$SLEEP_PID" 2>/dev/null
}

# ---------------------------------------------------------------- args
while (( $# > 0 )); do
    case "$1" in
        -l) [[ $# -ge 2 ]] || die "-l needs an argument"; LINK_DIR="$2"; shift 2 ;;
        -r) [[ $# -ge 2 ]] || die "-r needs an argument"; REMOTE="$2"; shift 2 ;;
        -i) [[ $# -ge 2 ]] || die "-i needs an argument"; INTERVAL="$2"; shift 2 ;;
        -1) ONCE=1; shift ;;
        -v) VERBOSE=1; shift ;;
        --disable-fsync) FSYNC=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage; die "unrecognized argument: $1" ;;
    esac
done

[[ -n "$LINK_DIR" ]] || { usage; die "-l is required"; }
[[ -n "$REMOTE" ]]   || { usage; die "-r is required"; }
[[ -d "$LINK_DIR" ]] || die "-l: not a directory: $LINK_DIR"
[[ "$REMOTE" == *:* && "$REMOTE" != :* && "$REMOTE" != *: ]] \
    || die "-r must look like host:remote_path/ (got '$REMOTE')"
[[ "$INTERVAL" =~ ^[1-9][0-9]*$ ]] || die "-i must be a positive integer (got '$INTERVAL')"
[[ "$REMOTE" == */ ]] || REMOTE="$REMOTE/"
REMOTE_HOST="${REMOTE%%:*}:"   # "host:" -- used in log messages

# Prime GATE_SEEN to the OPPOSITE of the startup state so that the first
# pass logs the actual state: present -> 0 (first pass logs "detected");
# absent -> 1 (first pass logs "no longer detected"). Afterwards it is
# only logged when the state changes.
if [[ -e "$LINK_DIR/$GATE_FILE" ]]; then GATE_SEEN=0; else GATE_SEEN=1; fi

RSYNC_OPTS=(
    --ignore-existing
    --out-format='%i %n'
    --timeout=30
    -e "$SSH_CMD -o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=5 -o ServerAliveCountMax=3"
)
if (( FSYNC )); then
    # Fail fast with a clear message rather than failing every pass.
    "$RSYNC_CMD" --help 2>&1 | grep -q -- '--fsync' \
        || die "this rsync has no --fsync (needs >= 3.2.4); use --disable-fsync"
    RSYNC_OPTS+=(--fsync)
fi

# --------------------------------------------------------------- setup
trap cleanup EXIT
trap on_signal INT TERM
ERRFILE="$(mktemp "${TMPDIR:-/tmp}/saf_link.XXXXXX")" || die "mktemp failed"

# Only one sender per link directory.
exec 9>"$LINK_DIR/.saf_link.lock" || die "cannot open lock file in $LINK_DIR"
flock -n 9 || die "another saf_link instance is running for $LINK_DIR"

# ------------------------------------------------------------ one pass
# Returns 0 delivered/nothing to do, 1 failed, 3 deferred.
one_pass() {
    local gate="$LINK_DIR/$GATE_FILE" src="$LINK_DIR/$ARCHIVE"
    local out rc

    if [[ ! -e "$gate" ]]; then
        # Say so once, on the pass where the gate file disappears (and,
        # below, once when it appears); stay quiet while the state is
        # unchanged, including an absent file that has never appeared.
        if (( GATE_SEEN )); then
            vlog "$GATE_FILE no longer detected in $LINK_DIR; link considered down, doing nothing"
            GATE_SEEN=0
        fi
        return 0
    fi
    if (( ! GATE_SEEN )); then
        vlog "$GATE_FILE detected in $LINK_DIR; link considered up"
        GATE_SEEN=1
    fi
    if [[ ! -f "$src" ]]; then
        return 0
    fi

    vlog "RSYNCing $src to $REMOTE_HOST"
    out="$("$RSYNC_CMD" "${RSYNC_OPTS[@]}" "$src" "${REMOTE}${ARCHIVE}" 2>"$ERRFILE" 9>&-)"
    rc=$?

    if (( rc != 0 )); then
        log_change "rsync failed (exit $rc); keeping local $ARCHIVE, will retry: $(head -n 3 "$ERRFILE" | tr '\n' ' ')"
        return 1
    fi

    # rsync created the remote file iff it itemized it as a NEW file:
    # "<f+++++++++ sending.zip" when pushing to a remote host ("<" = sent;
    # ">" appears for a purely local copy). A skipped file prints nothing.
    if printf '%s\n' "$out" | grep -Eq '^[<>]f\+{9} sending\.zip$'; then
        if rm -f -- "$src"; then
            log "RSYNCed $ARCHIVE to $REMOTE_HOST and deleted the local copy"
            LAST_MSG=""
            return 0
        fi
        log "ERROR: delivered $ARCHIVE but could not delete $src"
        return 1
    fi

    log_change "remote $ARCHIVE still exists (not yet consumed); keeping local $ARCHIVE, will retry"
    return 3
}

# ---------------------------------------------------------------- main
vlog "link dir: $LINK_DIR  remote: $REMOTE  interval: ${INTERVAL}s  fsync: $FSYNC"

while :; do
    one_pass
    rc=$?
    (( ONCE )) && exit "$rc"
    (( STOP )) && break
    sleep "$INTERVAL" 9>&- &
    SLEEP_PID=$!
    wait "$SLEEP_PID" 2>/dev/null
    SLEEP_PID=""
    (( STOP )) && break
done

vlog "stopping"
exit 0
