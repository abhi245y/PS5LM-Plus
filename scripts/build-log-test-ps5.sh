#!/usr/bin/env bash
# Compile only; never contacts the console. Requires the patched llama submodule.
set -euo pipefail
: "${PS5_PAYLOAD_SDK:?Set the extracted ps5-payload-sdk path}"
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
llama="$root/third_party/llama.cpp"
out="${1:-$root/build/log-backpressure-ps5.elf}"
git -C "$llama" apply --reverse --check "$root/patches/0006-log-never-blocks.patch"
mkdir -p "$(dirname "$out")"
"$PS5_PAYLOAD_SDK/bin/prospero-clang++" -std=c++17 -O2 -pthread \
    -fexceptions -fcxx-exceptions -frtti \
    -I"$root/tests" -I"$llama/common" -I"$llama/include" \
    -I"$llama/ggml/include" -I"$llama/vendor" \
    "$root/tests/log-backpressure-ps5.cpp" "$llama/common/json.cpp" -o "$out"
echo "PS5 logger test built: $out (not launched)"
