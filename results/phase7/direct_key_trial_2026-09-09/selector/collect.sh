set -Eeuo pipefail

repo=/home/icly/Documents/tetris_ai_runner
local_dir=$repo/results/phase7/direct_key_trial_2026-09-09/selector
artifact_dir=/home/icly/Documents/tetris_ai_runner_results/phase7/direct_key_trial_2026-09-09/selector_artifacts
normal=$artifact_dir/tetris_profile_normal
soa=$artifact_dir/tetris_profile_direct_key
params=$repo/artifacts/frozen_29d.bin
count_verdict=$repo/results/phase7/direct_key_trial_2026-09-09/count/PARENT_VERDICT.md
cpu15=/sys/devices/system/cpu/cpu15/online
log=$local_dir/collect.log
cpu15_offline=0
phase=preflight

if test -e "$log"; then
    printf '%s\n' "TERMINAL=BLOCKED reason=log_exists" >&2
    exit 28
fi
if ! : > "$log"; then
    printf '%s\n' "TERMINAL=BLOCKED reason=log_create" >&2
    exit 28
fi

record() {
    printf '%s\n' "$*" | tee -a "$log"
}

if test -e "$local_dir/rows.txt"; then
    record "TERMINAL=BLOCKED reason=rows_exists"
    exit 28
fi
for path in "$local_dir"/run{1..8}_*.txt "$local_dir"/run{1..8}_*.stderr.txt; do
    if test -e "$path"; then
        record "TERMINAL=BLOCKED reason=run_exists path=$path"
        exit 28
    fi
done

load_values() {
    awk '{print $1, $2, $3}' /proc/loadavg
}

load5_below() {
    awk -v value="$1" -v limit="$2" 'BEGIN { exit !(value < limit) }'
}

cleanup() {
    original_exit=$?
    trap - ERR EXIT
    set +e
    restore_result=not_needed
    restore_before=$(cat "$cpu15" 2>/dev/null || printf unknown)
    restore_after=$restore_before
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
    printf '%s\n' "CLEANUP_RESTORE utc=$(date -u +%FT%TZ 2>/dev/null || printf unknown) before=$restore_before after=$restore_after result=$restore_result" >> "$log" 2>/dev/null
    exit "$original_exit"
}

unexpected_error() {
    error_exit=$?
    trap - ERR
    if test "$phase" = preflight; then
        terminal=BLOCKED
    else
        terminal=ABORTED
    fi
    printf '%s\n' "TERMINAL=$terminal reason=unexpected_error exit=$error_exit line=${BASH_LINENO[0]} phase=$phase" >> "$log" 2>/dev/null || true
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
if ! git merge-base --is-ancestor 368a332 "$git_head" || test -n "$(git status --porcelain --untracked-files=no)"; then
    record "TERMINAL=BLOCKED reason=provenance"
    exit 28
fi
record "TRACKED_STATE=clean"

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

verify_hash 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765 "$normal"
verify_hash 420002a847f6fea6cb9e87b868f82ad1497bc0b12b7e817ebbe612c9dce6ee89 "$soa"
verify_hash ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037 "$params"
verify_hash 7b2e2dc7ecfb576afee3710b93465b8f914f591e5a685269be26fb6a14dc142e "$count_verdict"

if test "$(stat -c %a "$normal")" != 555 || test "$(stat -c %a "$soa")" != 555; then
    record "TERMINAL=BLOCKED reason=artifact_mode"
    exit 28
fi
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
    read -r load1 load5 load15 < <(load_values)
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
offline_start=$(date -u +%FT%TZ)
record "CPU15_OFFLINE_START=$offline_start state=$(cat "$cpu15")"

execute_command() {
    label=$1
    stdout_path=$2
    stderr_path=$3
    shift 3
    read -r load1 load5 load15 < <(load_values)
    state=$(cat "$cpu15")
    record "START label=$label utc=$(date -u +%FT%TZ) load1=$load1 load5=$load5 load15=$load15 cpu15=$state"
    if test "$state" != 0; then
        record "TERMINAL=ABORTED reason=cpu15_online label=$label"
        exit 35
    fi
    if ! load5_below "$load5" 2.0; then
        record "TERMINAL=ABORTED reason=start_load label=$label load5=$load5"
        exit 34
    fi
    printf 'COMMAND label=%s' "$label" >> "$log"
    printf ' %q' "$@" >> "$log"
    printf '\n' >> "$log"
    if "$@" > "$stdout_path" 2> "$stderr_path"; then
        exit_code=0
    else
        exit_code=$?
    fi
    end_state=$(cat "$cpu15")
    record "END label=$label utc=$(date -u +%FT%TZ) exit=$exit_code cpu15=$end_state"
    if test "$exit_code" != 0; then
        record "TERMINAL=ABORTED reason=nonzero_exit label=$label"
        exit 31
    fi
    if test "$end_state" != 0; then
        record "TERMINAL=ABORTED reason=cpu15_online_post label=$label"
        exit 35
    fi
}

run_prewarm() {
    label=$1
    binary=$2
    execute_command "$label" /dev/null /dev/null env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 "$binary" --seed 1 --warmup-moves 20 --moves 40 --maxdepth 6 --param-file "$params" --iters 200 --quiet --quiet-version 3 --telemetry off --timers off
}

run_prewarm prewarm_normal "$normal"
run_prewarm prewarm_soa "$soa"

wait_pair_load() {
    pair=$1
    read -r load1 load5 load15 < <(load_values)
    state=$(cat "$cpu15")
    record "PAIR_CHECK pair=$pair attempt=0 utc=$(date -u +%FT%TZ) load1=$load1 load5=$load5 load15=$load15 cpu15=$state"
    if test "$state" != 0; then
        record "TERMINAL=ABORTED reason=cpu15_online_wait pair=$pair"
        exit 35
    fi
    if load5_below "$load5" 2.0; then
        return
    fi
    for attempt in $(seq 1 5); do
        sleep 60
        read -r load1 load5 load15 < <(load_values)
        state=$(cat "$cpu15")
        record "PAIR_CHECK pair=$pair attempt=$attempt utc=$(date -u +%FT%TZ) load1=$load1 load5=$load5 load15=$load15 cpu15=$state"
        if test "$state" != 0; then
            record "TERMINAL=ABORTED reason=cpu15_online_wait pair=$pair"
            exit 35
        fi
        if load5_below "$load5" 1.5; then
            return
        fi
    done
    record "TERMINAL=ABORTED reason=pair_load pair=$pair"
    exit 33
}

run_timed() {
    number=$1
    variant=$2
    binary=$3
    execute_command "run${number}_${variant}" "$local_dir/run${number}_${variant}.txt" "$local_dir/run${number}_${variant}.stderr.txt" env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 "$binary" --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file "$params" --iters 1000 --quiet --quiet-version 3 --telemetry off --timers off
    if test "$(wc -l < "$local_dir/run${number}_${variant}.txt")" != 1 || ! grep -q '^PROFILE_V3 ' "$local_dir/run${number}_${variant}.txt" || test -s "$local_dir/run${number}_${variant}.stderr.txt"; then
        record "TERMINAL=ABORTED reason=invalid_row run=$number"
        exit 36
    fi
}

wait_pair_load 1
run_timed 1 normal "$normal"
run_timed 2 soa "$soa"
wait_pair_load 2
run_timed 3 soa "$soa"
run_timed 4 normal "$normal"
wait_pair_load 3
run_timed 5 soa "$soa"
run_timed 6 normal "$normal"
wait_pair_load 4
run_timed 7 normal "$normal"
run_timed 8 soa "$soa"

final_offline_state=$(cat "$cpu15")
record "COLLECTION_COMPLETE=$(date -u +%FT%TZ) cpu15=$final_offline_state"
if test "$final_offline_state" != 0; then
    record "TERMINAL=ABORTED reason=cpu_window_broken"
    exit 35
fi
if ! printf 1 | sudo -n tee "$cpu15" >/dev/null; then
    record "TERMINAL=ABORTED reason=cpu_restore_write"
    exit 35
fi
final_online_state=$(cat "$cpu15")
if test "$final_online_state" != 1; then
    record "TERMINAL=ABORTED reason=cpu_restore_state state=$final_online_state"
    exit 35
fi
cpu15_offline=0
offline_end=$(date -u +%FT%TZ)
record "CPU15_ONLINE_END=$offline_end state=$final_online_state"
record "TERMINAL=COLLECTED"
trap - ERR EXIT
