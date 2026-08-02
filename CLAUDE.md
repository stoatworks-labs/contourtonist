# CLAUDE.md — Contourtonist command reference

Read [AGENTS.md](AGENTS.md) first for what this project is and where its traps are. This
file is just the commands.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Reuse a local JUCE checkout instead of cloning:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/Projects/zero-eq/build/_deps/juce-src
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

The six DSP suites also build standalone with no CMake and no JUCE, which is much faster
when iterating on the maths:

```bash
clang++ -std=c++20 -O2 -o /tmp/t tests/test_equal_loudness.cpp Source/DSP/EqualLoudness.cpp && /tmp/t
```

Every suite prints its measured numbers (fit error, loop convergence, weighting deviation),
not just pass/fail. Read the output when changing DSP.

## Validate the plugin

```bash
auval -v aufx Ctn1 Alsg
```

## Verify the build is actually universal

The build log will not tell you. Only this will:

```bash
lipo -archs build/Contourtonist_artefacts/Release/VST3/Contourtonist.vst3/Contents/MacOS/Contourtonist
```

## Run the standalone

```bash
open build/Contourtonist_artefacts/Release/Standalone/Contourtonist.app
```

## Drive it without a meter

Send a level on the default port and watch the curve move:

```bash
python3 -c "
import socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for _ in range(120):
    s.sendto(b'88.0 C leq\n', ('127.0.0.1', 9878))
    time.sleep(0.25)
"
```

A bare number works too, so `nc` is a valid diagnostic:

```bash
echo "88.0" | nc -u -w1 127.0.0.1 9878
```
