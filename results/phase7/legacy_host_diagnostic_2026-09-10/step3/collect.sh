set -Eeuo pipefail

repo=/home/icly/Documents/tetris_ai_runner
results_root=/home/icly/Documents/tetris_ai_runner_results
local_dir=$repo/results/phase7/legacy_host_diagnostic_2026-09-10/step3
external_dir=$results_root/phase7/legacy_host_diagnostic_2026-09-10/step3
driver=$repo/out/build/linux-gcc-self-release/legacy_fast_adapter_bench
baseline=$results_root/phase7/tetris_profile.baseline
value_bin=$repo/out/build/linux-gcc-self-release/tetris_profile_value
params=$repo/artifacts/frozen_29d.bin
inputs=$results_root/phase7/row_export_fusion_trial_2026-09-09/trace/trace_normal.inputs.bin
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
if ! git merge-base --is-ancestor 578db3b "$git_head" || test -n "$(git status --porcelain --untracked-files=no)"; then
    record "TERMINAL=BLOCKED reason=provenance"
    exit 28
fi
record "TRACKED_STATE=clean"

if test -e "$local_dir/rows.txt" || test -e "$local_dir/digests.sha256"; then
    record "TERMINAL=BLOCKED reason=prior_outputs_exist"
    exit 28
fi
for path in "$local_dir"/run*.txt "$local_dir"/run*.stderr.txt "$local_dir"/anchor*.txt "$local_dir"/anchor*.stderr.txt; do
    if test -e "$path"; then
        record "TERMINAL=BLOCKED reason=run_exists path=$path"
        exit 28
    fi
done

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

verify_hash 84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376 "$baseline"
verify_hash 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765 "$value_bin"
verify_hash ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037 "$params"
verify_hash c6b5ecf6c2b7e956b7770ea957820d77fefc4fcaad0a9e1be22a55d1d1473da5 "$inputs"
record "HASH driver=$driver recorded in binaries.sha256 at finalization"

if test "$(stat -c %a "$baseline")" != 444; then
    record "TERMINAL=BLOCKED reason=baseline_mode"
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

mkdir -p "$external_dir"
cpu15_offline=1
if ! printf 0 | sudo -n tee "$cpu15" >/dev/null || test "$(cat "$cpu15")" != 0; then
    record "TERMINAL=BLOCKED reason=cpu_offline_write"
    exit 29
fi
phase=collection
offline_start=$(date -u +%FT%TZ)
record "CPU15_OFFLINE_START=$offline_start state=$(cat "$cpu15")"

run_capture() {
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

common_replay=(--mode replay --inputs "$inputs" --param-file "$params" --iters 1000 --maxdepth 6)

run_capture prewarm_driver "$external_dir/prewarm_driver.stdout.txt" "$external_dir/prewarm_driver.stderr.txt" "$driver" "${common_replay[@]}" --arm L1 --max-inputs 20
run_capture prewarm_baseline "$external_dir/prewarm_baseline.stdout.txt" "$external_dir/prewarm_baseline.stderr.txt" "$baseline" --seed 1 --warmup-moves 20 --moves 40 --maxdepth 6 --param-file "$params" --iters 200 --quiet --quiet-version 2 --telemetry off
run_capture prewarm_value "$external_dir/prewarm_value.stdout.txt" "$external_dir/prewarm_value.stderr.txt" "$value_bin" --seed 1 --warmup-moves 20 --moves 40 --maxdepth 6 --param-file "$params" --iters 200 --quiet --quiet-version 3 --telemetry off --timers off

anchor_slot=0
run_anchor() {
    anchor_slot=$((anchor_slot + 1))
    run_capture "anchor_${anchor_slot}_baseline" "$local_dir/anchor_${anchor_slot}_baseline.txt" "$local_dir/anchor_${anchor_slot}_baseline.stderr.txt" /usr/bin/time -v "$baseline" --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file "$params" --iters 1000 --quiet --quiet-version 2 --telemetry off
    run_capture "anchor_${anchor_slot}_value" "$local_dir/anchor_${anchor_slot}_value.txt" "$local_dir/anchor_${anchor_slot}_value.stderr.txt" "$value_bin" --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file "$params" --iters 1000 --quiet --quiet-version 3 --telemetry off --timers off
    run_capture "anchor_${anchor_slot}_l1" "$local_dir/anchor_${anchor_slot}_l1.txt" "$local_dir/anchor_${anchor_slot}_l1.stderr.txt" "$driver" --mode selfplay --arm L1 --param-file "$params" --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --iters 1000
}

run_round() {
    number=$1
    shift
    for arm in "$@"; do
        case "$arm" in
        L0) run_capture "run${number}_L0" "$local_dir/run${number}_L0.txt" "$local_dir/run${number}_L0.stderr.txt" "$driver" "${common_replay[@]}" --arm L0 ;;
        L1) run_capture "run${number}_L1" "$local_dir/run${number}_L1.txt" "$local_dir/run${number}_L1.stderr.txt" "$driver" "${common_replay[@]}" --arm L1 ;;
        L2) run_capture "run${number}_L2" "$local_dir/run${number}_L2.txt" "$local_dir/run${number}_L2.stderr.txt" "$driver" "${common_replay[@]}" --arm L2 ;;
        V) run_capture "run${number}_V" "$local_dir/run${number}_V.txt" "$local_dir/run${number}_V.stderr.txt" "$driver" "${common_replay[@]}" --arm V ;;
        esac
        if test -s "$local_dir/run${number}_${arm}.stderr.txt"; then
            record "TERMINAL=ABORTED reason=stderr_nonempty run=$number arm=$arm"
            exit 31
        fi
        grep -q '^DIAG_V1 ' "$local_dir/run${number}_${arm}.txt" || {
            record "TERMINAL=ABORTED reason=missing_diag_row run=$number arm=$arm"
            exit 36
        }
    done
}

run_round 1 L0 L1 L2 V
run_round 2 L1 V L0 L2
run_anchor
run_round 3 V L0 L2 L1
run_round 4 L0 V L1 L2
run_anchor
run_round 5 L1 L2 V L0
run_round 6 V L2 L0 L1
run_anchor

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

rows_file=$local_dir/rows.txt
: > "$rows_file"
for f in "$local_dir"/run*_*.txt; do
    cat "$f" >> "$rows_file"
done
{
    sha256sum "$local_dir"/run*_*.txt
    sha256sum "$external_dir"/prewarm_* 2>/dev/null || true
} > "$local_dir/digests.sha256"
{
    sha256sum "$driver" "$baseline" "$value_bin" "$params" "$inputs"
} > "$local_dir/binaries.sha256"

find "$external_dir" -maxdepth 1 -type f -exec chmod a-w {} +
record "TERMINAL=COLLECTED"
trap - ERR EXIT
