set -Eeuo pipefail

repo=/home/icly/Documents/tetris_ai_runner
stage=$1
case "$stage" in
    smoke) pairs=32; seed=910; stage_dir=$repo/results/phase7/quality_harness_2026-09-10/smoke ;;
    screen) pairs=128; seed=911; stage_dir=$repo/results/phase7/quality_harness_2026-09-10/screen ;;
    final) pairs=1000; seed=999; stage_dir=$repo/results/phase7/quality_harness_2026-09-10/final ;;
    *) printf 'unknown stage\n' >&2; exit 2 ;;
esac

log=$stage_dir/collect.log
csv=$stage_dir/matches.csv
cpu15=/sys/devices/system/cpu/cpu15/online
cpu15_offline=0
phase=preflight

if test -e "$log" || test -e "$csv"; then
    printf '%s\n' "TERMINAL=BLOCKED reason=stage_exists" >&2
    exit 28
fi
if ! : > "$log"; then
    printf '%s\n' "TERMINAL=BLOCKED reason=log_create" >&2
    exit 28
fi

record() {
    printf '%s\n' "$*" | tee -a "$log"
}

load5_below() {
    awk -v value="$1" -v limit="$2" 'BEGIN { exit !(value < limit) }'
}

cleanup() {
    original_exit=$?
    trap - ERR EXIT
    set +e
    restore_result=not_needed
    if test "$cpu15_offline" = 1; then
        restore_result=fail
        for restore_attempt in 1 2; do
            printf 1 | sudo -n tee "$cpu15" >/dev/null
            restore_write_exit=$?
            restore_after=$(cat "$cpu15" 2>/dev/null || printf unknown)
            if test "$restore_write_exit" = 0 && test "$restore_after" = 1; then
                restore_result="pass_attempt_$restore_attempt"
                cpu15_offline=0
                break
            fi
        done
    fi
    printf '%s\n' "CLEANUP_RESTORE utc=$(date -u +%FT%TZ 2>/dev/null || printf unknown) after=$restore_after result=$restore_result" >> "$log" 2>/dev/null
    exit "$original_exit"
}

unexpected_error() {
    error_exit=$?
    trap - ERR
    if test "$phase" = preflight; then
        terminal=BLOCKED
    else
        terminal=FAIL
    fi
    printf '%s\n' "TERMINAL=$terminal reason=unexpected_error exit=$error_exit line=${BASH_LINENO[0]}" >> "$log" 2>/dev/null || true
    exit "$error_exit"
}

trap cleanup EXIT
trap unexpected_error ERR

if ! cd "$repo"; then
    record "TERMINAL=BLOCKED reason=repo_unavailable"
    exit 28
fi

git_head=$(git rev-parse HEAD)
record "HEAD=$git_head"
if ! git merge-base --is-ancestor 059233f "$git_head" || test -n "$(git status --porcelain --untracked-files=no)"; then
    record "TERMINAL=BLOCKED reason=provenance"
    exit 28
fi
record "TRACKED_STATE=clean"

driver=$repo/out/build/linux-gcc-self-release/quality_match
params=$repo/artifacts/frozen_29d.bin
verify_hash() {
    expected=$1
    path=$2
    actual=$(sha256sum "$path" | awk '{print $1}')
    record "HASH path=$path expected=$expected actual=$actual"
    if test "$actual" != "$expected"; then
        record "TERMINAL=BLOCKED reason=hash path=$path"
        exit 28
    fi
}

verify_hash b92ee3b2e68661c1d822de25fddc532ed842a90cfaf2728cab9d44a4f6be9d9d "$driver"
verify_hash ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037 "$params"

if test "$(cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor)" != performance || test "$(cat /sys/devices/system/cpu/cpu7/cpufreq/energy_performance_preference)" != performance || test "$(cat /sys/devices/system/cpu/cpufreq/boost)" != 1 || test "$(cat "$cpu15")" != 1; then
    record "TERMINAL=BLOCKED reason=machine_control"
    exit 28
fi
if ! sudo -n test -w "$cpu15" || ! sudo -n true; then
    record "TERMINAL=BLOCKED reason=cpu_control_preflight"
    exit 29
fi
record "CPU_CONTROL_PREFLIGHT=pass CPU15=$(cat "$cpu15")"

precheck_pass=0
for attempt in $(seq 0 10); do
    read -r load1 load5 load15 < <(awk '{print $1, $2, $3}' /proc/loadavg)
    record "PRECHECK attempt=$attempt utc=$(date -u +%FT%TZ) load1=$load1 load5=$load5 load15=$load15"
    if load5_below "$load5" 1.5; then
        precheck_pass=1
        break
    fi
    if test "$attempt" -lt 10; then
        sleep 60
    fi
done
if test "$precheck_pass" != 1; then
    record "TERMINAL=BLOCKED reason=load_precheck"
    exit 30
fi

cpu15_offline=1
if ! printf 0 | sudo -n tee "$cpu15" >/dev/null || test "$(cat "$cpu15")" != 0; then
    record "TERMINAL=BLOCKED reason=cpu_offline_write"
    exit 29
fi
phase=collection
record "CPU15_OFFLINE_START=$(date -u +%FT%TZ) state=0"
record "STAGE=$stage pairs=$pairs seed=$seed threads=8 budget_ms=20"
record "COMMAND $driver match --param-a $params --param-b $params --pairs $pairs --ms 20 --seed $seed --threads 8 --max-rounds 3600 --out $csv --seats legacy,value"

if "$driver" match --param-a "$params" --param-b "$params" --pairs "$pairs" --ms 20 --seed "$seed" --threads 8 --max-rounds 3600 --out "$csv" --seats legacy,value >> "$log" 2>> "$stage_dir/match.stderr.txt"; then
    match_exit=0
else
    match_exit=$?
fi
record "MATCH_EXIT=$match_exit utc=$(date -u +%FT%TZ)"
if test "$match_exit" != 0; then
    record "TERMINAL=FAIL reason=match_nonzero exit=$match_exit"
    exit 31
fi
if test -s "$stage_dir/match.stderr.txt"; then
    record "TERMINAL=FAIL reason=match_stderr"
    exit 31
fi

final_state=$(cat "$cpu15")
if test "$final_state" != 0; then
    record "TERMINAL=FAIL reason=cpu_window_broken"
    exit 35
fi
if ! printf 1 | sudo -n tee "$cpu15" >/dev/null; then
    record "TERMINAL=FAIL reason=cpu_restore_write"
    exit 35
fi
final_online=$(cat "$cpu15")
if test "$final_online" != 1; then
    record "TERMINAL=FAIL reason=cpu_restore_state state=$final_online"
    exit 35
fi
cpu15_offline=0
record "CPU15_ONLINE_END=$(date -u +%FT%TZ) state=$final_online"
record "TERMINAL=COLLECTED"
trap - ERR EXIT
