#!/data/data/com.termux/files/usr/bin/bash
# Experimental K50 launcher: file-mapped host weights, Vulkan model operations.
set -euo pipefail
if [[ $# -lt 2 || $# -gt 3 ]]; then
    printf 'Usage: %s MODEL.gguf PROMPT [MAX_TOKENS]\n' "$0" >&2
    exit 2
fi
model=$1
prompt=$2
tokens=${3:-8}
if [[ ! -f "$model" ]]; then
    printf 'Model not found: %s\n' "$model" >&2
    exit 2
fi
if [[ ! "$tokens" =~ ^[1-9][0-9]*$ ]] || (( tokens > 256 )); then
    printf 'MAX_TOKENS must be an integer between 1 and 256.\n' >&2
    exit 2
fi
PREFIX=/data/data/com.termux/files/usr
export PREFIX PATH="$PREFIX/bin:/system/bin"
base="${MALI_LLAMA_BUILD:-$HOME/mali-clean-verify-20261102-lf/source/build/bin}"
vulkan="${MALI_MATVEC_LIBRARY_DIR:-$HOME/mali-clean-verify-20261102-lf/large-matvec-experiment}"
if [[ ! -x "$base/llama-completion" || ! -r "$vulkan/libggml-vulkan.so.0" ]]; then
    printf 'Experimental program or Vulkan library not found.\n' >&2
    exit 1
fi
export LD_LIBRARY_PATH="$vulkan:$base"
unset GGML_BACKEND_PATH
export GGML_NO_BACKTRACE=1 GGML_OP_OFFLOAD_MIN_BATCH=0
export GGML_VK_EXPERIMENTAL_LARGE_Q5_MAT=1 GGML_VK_EXPERIMENTAL_LARGE_Q6_MAT=1
export GGML_VK_EXPERIMENTAL_MALI_FP32_QUANTS=1 GGML_VK_EXPERIMENTAL_UNFUSED_ROPE_F16=1
export GGML_VK_EXPERIMENTAL_LARGE_MATVEC=1
trace_args=()
if [[ "${MALI_TRACE_GRAPH:-0}" == 1 ]]; then
    export GGML_SCHED_DEBUG=1
    trace_args+=(--verbose)
fi
chat_args=(-no-cnv)
if [[ "${MALI_CHAT:-0}" == 1 ]]; then
    chat_args=(-cnv -st --jinja)
fi

available_kib() {
    local key value unit
    while read -r key value unit; do
        if [[ "$key" == 'MemAvailable:' ]]; then printf '%s\n' "$value"; return 0; fi
    done < /proc/meminfo
    return 1
}
minimum_kib=700000
available=$(available_kib) || { printf 'Unable to read available system memory.\n' >&2; exit 1; }
if (( available < minimum_kib )); then
    printf 'Available memory is below the experimental limit; stopping.\n' >&2
    exit 1
fi
printf 'Experimental streamed weights: check the scheduler log for Vulkan placement.\n' >&2
"$base/llama-completion" \
    -m "$model" -ot '.*=CPU' -ngl 99 --no-host --no-repack --load-mode mmap \
    -c 256 -n "$tokens" -fa off -t 4 -b 16 -ub 8 \
    --temp 0 "${chat_args[@]}" --no-warmup -fit off "${trace_args[@]}" -p "$prompt" &
child=$!
trap 'kill -TERM "$child" 2>/dev/null || true' INT TERM
while kill -0 "$child" 2>/dev/null; do
    if available=$(available_kib); then
        if (( available < minimum_kib )); then
            printf '\nAvailable memory fell below the experimental limit; stopping this inference.\n' >&2
            kill -TERM "$child" 2>/dev/null || true
            break
        fi
    else
        printf '\nUnable to monitor system memory; stopping this inference.\n' >&2
        kill -TERM "$child" 2>/dev/null || true
        break
    fi
    sleep 2
done
set +e
wait "$child"
status=$?
set -e
exit "$status"
