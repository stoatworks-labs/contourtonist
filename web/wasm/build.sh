#!/bin/bash
# Builds the Contourtonist DSP core (the plugin's own Source/DSP files, unmodified) to
# WebAssembly. No shim is needed: Source/DSP depends on nothing but the C++ standard
# library, by design (see AGENTS.md §3). Output is a single self-contained ES module
# (wasm embedded, synchronous compile) so the same file loads in an AudioWorklet, on
# the main thread, and in Node for the verification harness.
set -euo pipefail
cd "$(dirname "$0")"

SRC=../../Source

emcc -O3 -std=c++20 \
  -I "$SRC" \
  contourtonist_web.cpp \
  "$SRC/DSP/EqualLoudness.cpp" \
  "$SRC/DSP/LoudnessController.cpp" \
  "$SRC/DSP/ContourFilterBank.cpp" \
  "$SRC/DSP/Biquad.cpp" \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=createContourtonistModule \
  -sSINGLE_FILE=1 \
  -sWASM_ASYNC_COMPILATION=0 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web,worker,node \
  -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,HEAPF32 \
  -sINCOMING_MODULE_JS_API=instantiateWasm,locateFile \
  -o ../public/contourtonist.js

echo "Built ../public/contourtonist.js ($(du -h ../public/contourtonist.js | cut -f1))"
