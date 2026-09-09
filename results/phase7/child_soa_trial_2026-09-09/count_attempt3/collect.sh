set -Eeuo pipefail

repo=/home/icly/Documents/tetris_ai_runner
local_dir=$repo/results/phase7/child_soa_trial_2026-09-09/count_attempt3
external_dir=/home/icly/Documents/tetris_ai_runner_results/phase7/child_soa_trial_2026-09-09/trace_attempt3
normal=$repo/out/build/linux-gcc-self-release/tetris_profile_value
soa=$repo/out/build/linux-gcc-self-release/tetris_profile_child_soa
partition_normal=$repo/out/build/linux-gcc-self-release/candidate_partition
partition_soa=$repo/out/build/linux-gcc-self-release/candidate_partition_child_soa
params=$repo/artifacts/frozen_29d.bin
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

if test -e "$external_dir"; then
    record "TERMINAL=BLOCKED reason=external_exists"
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
    chmod_exit=0
    if test -d "$external_dir"; then
        find "$external_dir" -maxdepth 1 -type f -exec chmod a-w {} +
        chmod_exit=$?
    fi
    printf '%s\n' "CLEANUP_RESTORE utc=$(date -u +%FT%TZ 2>/dev/null || printf unknown) before=$restore_before after=$restore_after result=$restore_result chmod_exit=$chmod_exit" >> "$log" 2>/dev/null
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
if ! git merge-base --is-ancestor bffa256 "$git_head" || test -n "$(git status --porcelain --untracked-files=no)"; then
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
verify_hash 721394d9bec61fce9654266fa0fdc9b41d921d4bba1673b8b931fbc1fb0c9c91 "$soa"
verify_hash 5986bdeebf9ff34fe4c3de0845881991fe463f6dc84dcbfb8d1b4bd7944642a1 "$partition_normal"
verify_hash 6b5b520fdf253def718b3743f8fd53d54eb7636329c83f9c39b2510a65fa582a "$partition_soa"
verify_hash ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037 "$params"
verify_hash 82a22a0cfd7afc288b2255007aca0c82bb3278b49544f9bbd0092e6e3ee797d0 "$repo/results/phase7/child_soa_trial_2026-09-09/validation/prerequisite_tests_2026-09-09.txt"

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
    if [[ "$label" = run* ]] && ! load5_below "$load5" 2.0; then
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

run_capture trace_normal "$external_dir/trace_normal.stdout.txt" "$external_dir/trace_normal.stderr.txt" env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 "$partition_normal" record --seed 1 --warmup-moves 0 --moves 20 --maxdepth 6 --param-file "$params" --iters 1000 --record-out "$external_dir/trace_normal"
run_capture trace_soa "$external_dir/trace_soa.stdout.txt" "$external_dir/trace_soa.stderr.txt" env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 "$partition_soa" record --seed 1 --warmup-moves 0 --moves 20 --maxdepth 6 --param-file "$params" --iters 1000 --record-out "$external_dir/trace_soa"
if test -s "$external_dir/trace_normal.stderr.txt" || test -s "$external_dir/trace_soa.stderr.txt"; then
    record "TERMINAL=FAIL reason=trace_stderr"
    exit 32
fi

for suffix in inputs.bin moves.tsv run_totals.tsv; do
    if ! cmp "$external_dir/trace_normal.$suffix" "$external_dir/trace_soa.$suffix"; then
        record "TERMINAL=FAIL reason=trace_byte_mismatch suffix=$suffix"
        exit 32
    fi
done

if ! python3 - "$external_dir/trace_normal.stdout.txt" "$external_dir/trace_soa.stdout.txt" <<'PY'
from pathlib import Path
import sys

def parse(path):
    text = Path(path).read_text().strip()
    if not text.startswith("PROFILE_V3 "):
        raise SystemExit(1)
    return dict(field.split("=", 1) for field in text.split()[1:])

def timing(key):
    return key == "total_s" or key.endswith("_ms") or key.endswith("_ns") or key.endswith("_per_s")

normal = parse(sys.argv[1])
soa = parse(sys.argv[2])
if set(normal) != set(soa):
    raise SystemExit(1)
for key in normal:
    if not timing(key) and key != "mem_retained_bytes" and normal[key] != soa[key]:
        raise SystemExit(1)
if normal["mem_retained_bytes"] != "266338276" or soa["mem_retained_bytes"] != "266153956":
    raise SystemExit(1)
PY
then
    record "TERMINAL=FAIL reason=trace_stdout_mapping"
    exit 32
fi
record "TRACE_IDENTITY=pass"
find "$external_dir" -maxdepth 1 -type f -exec chmod a-w {} +

wait_half_load() {
    half=$1
    read -r load1 load5 load15 < <(load_values)
    state=$(cat "$cpu15")
    record "HALF_CHECK half=$half attempt=0 utc=$(date -u +%FT%TZ) load1=$load1 load5=$load5 load15=$load15 cpu15=$state"
    if test "$state" != 0; then
        record "TERMINAL=ABORTED reason=cpu15_online_wait half=$half"
        exit 35
    fi
    if load5_below "$load5" 2.0; then
        return
    fi
    for attempt in $(seq 1 5); do
        sleep 60
        read -r load1 load5 load15 < <(load_values)
        state=$(cat "$cpu15")
        record "HALF_CHECK half=$half attempt=$attempt utc=$(date -u +%FT%TZ) load1=$load1 load5=$load5 load15=$load15 cpu15=$state"
        if test "$state" != 0; then
            record "TERMINAL=ABORTED reason=cpu15_online_wait half=$half"
            exit 35
        fi
        if load5_below "$load5" 1.5; then
            return
        fi
    done
    record "TERMINAL=ABORTED reason=half_load half=$half"
    exit 33
}

run_count() {
    number=$1
    variant=$2
    binary=$3
    if test "$number" = 1 || test "$number" = 5; then
        wait_half_load "$number"
    else
        read -r load1 load5 load15 < <(load_values)
        state=$(cat "$cpu15")
        record "MID_CHECK run=$number utc=$(date -u +%FT%TZ) load1=$load1 load5=$load5 load15=$load15 cpu15=$state"
        if test "$state" != 0; then
            record "TERMINAL=ABORTED reason=cpu15_online_wait run=$number"
            exit 35
        fi
        if ! load5_below "$load5" 2.0; then
            record "TERMINAL=ABORTED reason=mid_half_load run=$number load5=$load5"
            exit 34
        fi
    fi
    run_capture "run${number}_${variant}" "$local_dir/run${number}_${variant}.txt" "$local_dir/run${number}_${variant}.stderr.txt" env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 "$binary" --seed 1 --warmup-moves 20 --moves 80 --maxdepth 6 --param-file "$params" --iters 1000 --quiet --quiet-version 3 --telemetry on --timers off
    if test "$(wc -l < "$local_dir/run${number}_${variant}.txt")" != 1 || ! grep -q '^PROFILE_V3 ' "$local_dir/run${number}_${variant}.txt" || test -s "$local_dir/run${number}_${variant}.stderr.txt"; then
        record "TERMINAL=ABORTED reason=invalid_row run=$number"
        exit 36
    fi
}

run_count 1 normal "$normal"
run_count 2 soa "$soa"
run_count 3 soa "$soa"
run_count 4 normal "$normal"
run_count 5 normal "$normal"
run_count 6 soa "$soa"
run_count 7 soa "$soa"
run_count 8 normal "$normal"

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
