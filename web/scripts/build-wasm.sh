#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

emcc ../src/wasm_core.cpp -o src/arena_wasm.js \
  -std=c++17 -O3 -flto -lembind \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16MB \
  -sWASM_BIGINT=1 -sMALLOC=emmalloc -sFILESYSTEM=0 -sASSERTIONS=0 \
  "-sEXPORTED_FUNCTIONS=['_malloc','_free']" \
  "-sEXPORTED_RUNTIME_METHODS=['wasmMemory']"

echo "Built src/arena_wasm.{js,wasm}"
ls -la src/arena_wasm.js src/arena_wasm.wasm
