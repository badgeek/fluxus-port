# Vendored: NoiseWorkshop — REFERENCE ONLY (not built)

Source: https://github.com/andreasmuller/NoiseWorkshop.git (Andreas Müller,
"Noise Workshop", Resonate 2015). Vendored 2026-09-04 for reference — the CMake
build does NOT compile any of this; it's here to read.

It's an **openFrameworks** (oF 0.8.4) workshop, not a library. The value for this
port is the GLSL, under each project's `bin/data/Shaders/**` (`.frag` / `.vert` /
`.glslinc`): fBm/Perlin/simplex noise, domain warping, noise terrain + grid,
step/shaping/threshold functions, GPU ping-pong particle systems (spawn textures,
update/draw passes), a raymarched volumetric cloud shader, grass, and a
live-coding surface. See the top-level `README.md` for the workshop's project
order and which shaders each stage focuses on.

Pruned from the upstream clone: `.git`, Xcode/VS per-user state
(`*.xcuserstate`, `*.xccheckout`, `xcuserdata/`, `DerivedData/`), `.DS_Store`.
Kept: all source, shaders, project files, and sample media. GLSL here is desktop
GL2/GL3 for oF — adapt to this port's `post-shader` / builtin-shader conventions
rather than dropping it in verbatim.
