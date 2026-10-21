#!/usr/bin/env bash
# Tests for saf_link_ssh_prototype.bash. Needs real rsync (>= 3.2.4) and
# flock. ssh is replaced by a tiny stand-in that drops the host name and
# runs the remote command locally, so rsync's real behavior
# (--ignore-existing, --out-format, --fsync, temp-file rename) is
# exercised without a second machine. Run: bash test_saf_link_ssh_prototype.bash
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="$HERE/../saf_link_ssh_prototype.bash"
FAILS=0

TMP="$(mktemp -d /tmp/saf_link_test.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# --- ssh stand-in: skip "-o VAL" pairs and the host, run the rest locally
cat > "$TMP/fake_ssh" <<'EOF'
#!/usr/bin/env bash
while [[ "$1" == -o ]]; do shift 2; done
shift                      # host
exec "$@"
EOF
# --- ssh stand-in that always fails like an unreachable host
cat > "$TMP/dead_ssh" <<'EOF'
#!/usr/bin/env bash
echo "ssh: connect to host: Network is unreachable" >&2
exit 255
EOF
# --- rsync wrapper that records its arguments, then runs the real rsync
cat > "$TMP/spy_rsync" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$SPY_LOG"
exec rsync "$@"
EOF
chmod +x "$TMP"/fake_ssh "$TMP"/dead_ssh "$TMP"/spy_rsync

check() {   # check "description" command...
    local desc="$1"; shift
    if "$@"; then echo "PASS: $desc"; else echo "FAIL: $desc"; FAILS=$((FAILS+1)); fi
}

new_case() {  # sets L (local link dir) and R (remote inbound dir)
    L="$TMP/$1/local/link"; R="$TMP/$1/remote/inbound"
    mkdir -p "$L" "$R"
}

run() {  # run the script one pass with the fake ssh; extra args appended
    SAF_SSH="$TMP/fake_ssh" "$SCRIPT" -l "$L" -r "fakehost:$R" -1 "$@" 2>"$TMP/stderr"
}

# 1 -- no archive: nothing happens, exit 0
new_case c1; touch "$L/link.is.active"
run; rc=$?
check "1 no sending.zip -> exit 0, nothing created remotely" \
    bash -c "[[ $rc == 0 && -z \"\$(ls -A '$R')\" ]]"

# 2 -- archive but no gate file: local kept, nothing sent
new_case c2; echo data > "$L/sending.zip"
run; rc=$?
check "2 no link.is.active -> exit 0, local kept, remote empty" \
    bash -c "[[ $rc == 0 && -f '$L/sending.zip' && -z \"\$(ls -A '$R')\" ]]"

# 3 -- normal delivery
new_case c3; touch "$L/link.is.active"; head -c 100000 /dev/urandom > "$L/sending.zip"
cp "$L/sending.zip" "$TMP/c3.orig"
run; rc=$?
check "3 delivery -> exit 0, local deleted, remote identical, no temp files left" \
    bash -c "[[ $rc == 0 && ! -e '$L/sending.zip' ]] && cmp -s '$R/sending.zip' '$TMP/c3.orig' && [[ \$(ls -A '$R' | wc -l) == 1 ]]"

# 4 -- remote already has a different sending.zip: untouched, local kept, exit 3
new_case c4; touch "$L/link.is.active"
echo NEW > "$L/sending.zip"; echo OLD-UNCONSUMED > "$R/sending.zip"
run; rc=$?
check "4 remote sending.zip exists -> exit 3, remote untouched, local kept" \
    bash -c "[[ $rc == 3 && \$(cat '$R/sending.zip') == OLD-UNCONSUMED && \$(cat '$L/sending.zip') == NEW ]]"

# 4b -- once the remote consumer deletes it, the same local file goes through
rm -f "$R/sending.zip"
run; rc=$?
check "4b after remote consumer deletes it -> delivered, local deleted" \
    bash -c "[[ $rc == 0 && \$(cat '$R/sending.zip') == NEW && ! -e '$L/sending.zip' ]]"

# 5 -- ssh failure: exit 1, local kept
new_case c5; touch "$L/link.is.active"; echo data > "$L/sending.zip"
SAF_SSH="$TMP/dead_ssh" "$SCRIPT" -l "$L" -r "fakehost:$R" -1 2>"$TMP/stderr"; rc=$?
check "5 ssh unreachable -> exit 1, local kept" \
    bash -c "[[ $rc == 1 && -f '$L/sending.zip' && -z \"\$(ls -A '$R')\" ]]"

# 6 -- fsync on by default, off with --disable-fsync
new_case c6; touch "$L/link.is.active"; echo data > "$L/sending.zip"
export SPY_LOG="$TMP/spy6.log"; : > "$SPY_LOG"
SAF_RSYNC="$TMP/spy_rsync" run
check "6a default passes --fsync" grep -q -- '--fsync' "$SPY_LOG"
new_case c6b; touch "$L/link.is.active"; echo data > "$L/sending.zip"
: > "$SPY_LOG"
SAF_RSYNC="$TMP/spy_rsync" run --disable-fsync; rc=$?
check "6b --disable-fsync omits --fsync (and still delivers)" \
    bash -c "! grep -q -- '--fsync' '$SPY_LOG' && [[ $rc == 0 && -f '$R/sending.zip' ]]"

# 7 -- argument errors exit 2
"$SCRIPT" 2>/dev/null; rc=$?
check "7a no arguments -> exit 2" test "$rc" = 2
new_case c7
"$SCRIPT" -l "$L" -r "nocolon" -1 2>/dev/null; rc=$?
check "7b -r without host:path -> exit 2" test "$rc" = 2
"$SCRIPT" -l "$L" -r "h:/x/" -i 0 -1 2>/dev/null; rc=$?
check "7c -i 0 -> exit 2" test "$rc" = 2
"$SCRIPT" -l "$TMP/nonexistent" -r "h:/x/" -1 2>/dev/null; rc=$?
check "7d -l not a directory -> exit 2" test "$rc" = 2

# 8 -- second instance refused while the lock is held
new_case c8
( exec 9>"$L/.saf_link.lock"; flock -n 9; sleep 3 ) &
holder=$!; sleep 0.5
SAF_SSH="$TMP/fake_ssh" "$SCRIPT" -l "$L" -r "fakehost:$R" -1 2>/dev/null; rc=$?
check "8 second instance while lock held -> exit 2" test "$rc" = 2
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null

# 9 -- loop mode: waits for gate + archive, delivers, stops cleanly on TERM
new_case c9
SAF_SSH="$TMP/fake_ssh" "$SCRIPT" -l "$L" -r "fakehost:$R" -i 1 2>"$TMP/stderr9" &
pid=$!
sleep 1.2
echo LOOP > "$L/sending.zip"                  # no gate yet -> must wait
sleep 1.5
check "9a loop: archive without gate is not sent" \
    bash -c "[[ -f '$L/sending.zip' && -z \"\$(ls -A '$R')\" ]]"
touch "$L/link.is.active"
for _ in 1 2 3 4 5 6; do [[ -f "$R/sending.zip" ]] && break; sleep 1; done
check "9b loop: delivered once gate appears, local deleted" \
    bash -c "[[ \$(cat '$R/sending.zip') == LOOP && ! -e '$L/sending.zip' ]]"
kill -TERM "$pid"; wait "$pid"; rc=$?
check "9c loop: SIGTERM -> clean exit 0" test "$rc" = 0

echo
if (( FAILS == 0 )); then echo "All tests passed."; else echo "$FAILS test(s) FAILED."; exit 1; fi
