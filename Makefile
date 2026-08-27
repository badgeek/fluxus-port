# fluxus -> JUCE port — convenience wrapper over the CMake/Ninja build.
#
#   make run            # build + run the GL Racket app (FluxusGLRacketApp)
#   make run SCRIPT=examples/walk.scm   # ...loading a starter script
#   make gl-racket      # build that target only
#   make configure      # first-time CMake configure (fetches JUCE)
#   make build          # build every target
#   make s7 gl racket   # build the other three apps
#   make run-s7 / run-gl / run-racket
#   make clean          # remove the build dir
#
# The run targets exec the app's inner Mach-O binary directly (not `open`) so it
# inherits THIS terminal's TCC grant — the microphone (gh)/(gain) input works.

BUILD    ?= build
CONFIG   ?= Release
GENERATOR ?= Ninja
SCRIPT   ?=

CMAKE    ?= cmake
REL       = $(BUILD)/$@_artefacts/$(CONFIG)

# app target -> inner binary path
define APP_BIN
$(BUILD)/$(1)_artefacts/$(CONFIG)/$(1).app/Contents/MacOS/$(1)
endef

# build + run one app target ($1), passing SCRIPT through FLUXUS_SCRIPT
define RUN_APP
$(CMAKE) --build $(BUILD) --target $(1)
@echo "== running $(1) =="
FLUXUS_SCRIPT="$(SCRIPT)" "$(call APP_BIN,$(1))"
endef

RACKET   ?= /opt/homebrew/Cellar/minimal-racket/9.3/bin/racket
RACKET_LIB = racket-lib
# the .ss files the Racket hosts require (see RacketScriptHost::requireLibForm)
LIBSS = fluxus-modules building-blocks maths randomness poly-tools shapes \
        input camera mouse help pixels-tools voxels-tools planetarium \
        collada-import fluxus-engine

.PHONY: all configure build clean precompile \
        run run-s7 run-gl run-racket \
        gl-racket racket s7 gl

all: build

# --- precompile the fluxus .ss to bytecode (racket-lib/compiled/*.zo) --------
# Optional: the embedded Racket already loads the compiled COLLECTS fast (the app
# sets current-compiled-file-roots), so this only shaves the last ~1s of .ss
# expansion. Regenerate after editing any racket-lib/*.ss.
precompile:
	$(RACKET) -e '(require compiler/cm) (for-each managed-compile-zo (list \
	  $(foreach f,$(LIBSS),"$(RACKET_LIB)/$(f).ss")))'
	@echo "precompiled: $$(find $(RACKET_LIB) -name '*.zo' | wc -l | tr -d ' ') .zo"

# --- configure (first run fetches JUCE + FreeType) --------------------------
$(BUILD):
	$(CMAKE) -S . -B $(BUILD) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=$(CONFIG)

configure: $(BUILD)

build: | $(BUILD)
	$(CMAKE) --build $(BUILD)

# --- build single targets ---------------------------------------------------
gl-racket: | $(BUILD) ; $(CMAKE) --build $(BUILD) --target FluxusGLRacketApp
racket:    | $(BUILD) ; $(CMAKE) --build $(BUILD) --target FluxusRacketApp
s7:        | $(BUILD) ; $(CMAKE) --build $(BUILD) --target FluxusApp
gl:        | $(BUILD) ; $(CMAKE) --build $(BUILD) --target FluxusGLApp

# --- build + run ------------------------------------------------------------
run: | $(BUILD)         ## GL Racket app (fluxus GLEditor + real Racket)
	$(call RUN_APP,FluxusGLRacketApp)
run-racket: | $(BUILD)  ## JUCE editor + real Racket
	$(call RUN_APP,FluxusRacketApp)
run-s7: | $(BUILD)      ## JUCE editor + s7 Scheme
	$(call RUN_APP,FluxusApp)
run-gl: | $(BUILD)      ## fluxus GLEditor + s7 Scheme
	$(call RUN_APP,FluxusGLApp)

clean:
	rm -rf $(BUILD)
