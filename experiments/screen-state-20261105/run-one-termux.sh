#!/data/data/com.termux/files/usr/bin/bash
# Reproduction helper for the 2026-11-05 single-device experiment.
set -euo pipefail
if [[ $# != 4 || ( $1 != short && $1 != chat32 ) ]]; then
    printf 'Usage: %s short|chat32 MODEL.gguf PROBE_BINARY LOGITS_OUT\n' "$0" >&2
    exit 2
fi
: "${MALI_LLAMA_BUILD:?Set MALI_LLAMA_BUILD to the llama.cpp build/bin directory}"
: "${MALI_VULKAN_LIB:?Set MALI_VULKAN_LIB to the experimental Vulkan library directory}"
mode=$1; model=$2; probe=$3; result=$4
here=$(cd "$(dirname "$0")" && pwd)
[[ -r "$model" && -x "$probe" ]] || { echo 'Model or probe unavailable' >&2; exit 2; }
available() { awk '/^MemAvailable:/ {print $2; exit}' /proc/meminfo; }
initial=$(available)
[[ "$initial" =~ ^[0-9]+$ ]] && (( initial >= 3300000 )) || { echo 'Insufficient available memory' >&2; exit 2; }
export LD_LIBRARY_PATH="$MALI_VULKAN_LIB:$MALI_LLAMA_BUILD" GGML_NO_BACKTRACE=1 GGML_SCHED_DEBUG=1 GGML_OP_OFFLOAD_MIN_BATCH=0
export GGML_VK_EXPERIMENTAL_LARGE_Q5_MAT=1 GGML_VK_EXPERIMENTAL_LARGE_Q6_MAT=1
export GGML_VK_EXPERIMENTAL_MALI_FP32_QUANTS=1 GGML_VK_EXPERIMENTAL_UNFUSED_ROPE_F16=1 GGML_VK_EXPERIMENTAL_LARGE_MATVEC=1
export MALI_PROBE_STREAM_WEIGHTS=1 MALI_PROBE_RESIDENT_OUTPUT=1 MALI_PROBE_RESIDENT_EMBED=1 MALI_PROBE_RESIDENT_FFN4=1 MALI_PROBE_GENERATE_8=1
if [[ "$mode" == short ]]; then
    export MALI_PROBE_MAX_GENERATE=8
    unset MALI_PROBE_CHAT_FORMAT MALI_PROBE_CHAT_SYSTEM
    prompt="$here/prompt-english.txt"
else
    export MALI_PROBE_MAX_GENERATE=32 MALI_PROBE_CHAT_FORMAT=1 MALI_PROBE_CHAT_SYSTEM=1
    prompt="$here/prompt-chinese.txt"
fi
timeout -k 5 240 "$probe" "$model" gpu "$result" no-flash "file:$prompt" &
child=$!
trap 'kill -TERM "$child" 2>/dev/null || true' INT TERM
stopped=0
while kill -0 "$child" 2>/dev/null; do
    current=$(available) || current=''
    if [[ ! "$current" =~ ^[0-9]+$ ]] || (( current < 1800000 )); then
        echo 'Memory monitor stopped the experiment' >&2
        stopped=1
        kill -TERM "$child" 2>/dev/null || true
        break
    fi
    sleep 2
done
set +e
wait "$child"
status=$?
set -e
(( stopped == 0 )) || exit 125
exit "$status"
