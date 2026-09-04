# --- ntsc-rs (valadaptive/ntsc-rs) final-stage NTSC/VHS filter --------------
# Vendored Rust core crate (vendor/ntsc-rs/ntscrs, MIT OR ISC OR Apache-2.0)
# + a tiny C-ABI staticlib wrapper (vendor/ntsc-rs/ffi). Built via cargo into
# ${CMAKE_BINARY_DIR}/ntsc-ffi and linked as an IMPORTED static lib.
# app/NTSCEffect.cpp drives it (replaces the old LMP88959/NTSC-CRT engine).
#
# Toolchain notes:
# - MSRV ~1.89: use the rustup-managed cargo in ~/.cargo/bin if present (a nix/
#   brew rustc on PATH may be older and shadows it — RUSTC is pinned to match).
# - Deps are fetched from crates.io on the first build (Cargo.lock committed).

set(NTSC_RS_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ntsc-rs)
set(NTSC_RS_OUT   ${CMAKE_BINARY_DIR}/ntsc-ffi)
set(NTSC_RS_LIB   ${NTSC_RS_OUT}/release/libntsc_ffi.a)

find_program(NTSC_CARGO cargo HINTS $ENV{HOME}/.cargo/bin PATHS $ENV{HOME}/.cargo/bin NO_DEFAULT_PATH)
if(NOT NTSC_CARGO)
  find_program(NTSC_CARGO cargo)
endif()
if(NOT NTSC_CARGO)
  message(FATAL_ERROR "cargo not found — the NTSC filter is built from Rust "
          "(vendor/ntsc-rs). Install rustup (https://rustup.rs) and run "
          "'rustup update stable' (needs rustc >= 1.89).")
endif()

# Pin RUSTC to the toolchain next to the chosen cargo when it's the rustup one,
# so an older rustc earlier on PATH can't hijack the build.
get_filename_component(NTSC_CARGO_DIR ${NTSC_CARGO} DIRECTORY)
if(EXISTS ${NTSC_CARGO_DIR}/rustc)
  set(NTSC_RUSTC_ENV ${CMAKE_COMMAND} -E env RUSTC=${NTSC_CARGO_DIR}/rustc)
else()
  set(NTSC_RUSTC_ENV ${CMAKE_COMMAND} -E env)
endif()

file(GLOB_RECURSE NTSC_RS_SOURCES
     ${NTSC_RS_DIR}/ffi/src/*.rs
     ${NTSC_RS_DIR}/ntscrs/src/*.rs)

add_custom_command(
  OUTPUT ${NTSC_RS_LIB}
  COMMAND ${NTSC_RUSTC_ENV} ${NTSC_CARGO} build --release
          --manifest-path ${NTSC_RS_DIR}/Cargo.toml
          --package ntsc-ffi
          --target-dir ${NTSC_RS_OUT}
  DEPENDS ${NTSC_RS_SOURCES}
          ${NTSC_RS_DIR}/Cargo.toml
          ${NTSC_RS_DIR}/ffi/Cargo.toml
          ${NTSC_RS_DIR}/ntscrs/Cargo.toml
  WORKING_DIRECTORY ${NTSC_RS_DIR}
  COMMENT "cargo build --release (ntsc-ffi)"
  VERBATIM)
add_custom_target(ntsc_ffi_build DEPENDS ${NTSC_RS_LIB})

add_library(ntsc_ffi STATIC IMPORTED GLOBAL)
set_target_properties(ntsc_ffi PROPERTIES IMPORTED_LOCATION ${NTSC_RS_LIB})
add_dependencies(ntsc_ffi ntsc_ffi_build)
