# Scripting-host spikes

Proof-of-concept for the `IScriptHost` seam (see `../DESIGN.md` §6a). Two
candidate embedded languages bound to an identical fake engine (`common/SpikeEngine.h`,
mirroring fluxus's `Engine::Get()` singleton). Each proves: embed, bind C++
calls, eval a **live** code string, **hot re-eval** at runtime, capture console
output, catch a script error without crashing.

## Build + run

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/spike_s7      # s7 Scheme
./build/spike_lua     # Lua + sol2
```

First configure fetches s7 (radiganm/s7), Lua 5.4 (walterschell/Lua), sol2
(ThePhD/sol2) via CMake FetchContent.

## Outcome

Both pass. Lua/sol2 = least binding code + auto value-marshalling + fastest
build. s7 = keeps Scheme syntax + live-coding, one-file build, more manual glue.
Decision deferred to port Phase 3; the seam makes it swappable.
