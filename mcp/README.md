# fluxus-mcp — drive the live-coding buffer over MCP

An MCP (stdio) server that lets an agent load / edit / save / screenshot the
sketch running in a Fluxus app. It bridges MCP tool calls to the app's localhost
control server (`app/ControlServer.cpp`), which writes the **same** buffer the
on-screen editor does — so remote edits show in the editor and re-run live.

## Build

```sh
cd mcp && go build -o fluxus-mcp .
```

## Run

1. Launch a JUCE-editor app with the control port set (enables the server; it's
   off otherwise):

   ```sh
   FLUXUS_CONTROL_PORT=8020 open build/FluxusRacketApp_artefacts/Release/FluxusRacketApp.app
   #                    …or FluxusApp (s7)
   ```

2. Point an MCP client at the server. This repo ships `.mcp.json` for Claude Code
   (command `./mcp/fluxus-mcp`, `FLUXUS_CONTROL_PORT=8020`). The server talks to
   the app over TCP `127.0.0.1:$FLUXUS_CONTROL_PORT` (default 8020).

## Tools

| tool | does |
|------|------|
| `fluxus_load(code)` | replace the running sketch + evaluate; returns `ok` or the eval error |
| `fluxus_get()` | the sketch source currently running (read → edit → `fluxus_load`) |
| `fluxus_error()` | last eval error (`''` = ok) |
| `fluxus_save(path)` | write the running sketch to a `.scm` file |
| `fluxus_screenshot(path?)` | grab the current frame to a PNG; returns the path (read the file to view) |

## Notes

- The control server only starts when `FLUXUS_CONTROL_PORT` is set — no open port
  by default. It binds `127.0.0.1` only (local).
- Only the JUCE-editor apps (`FluxusApp`, `FluxusRacketApp`) have it; the GLEditor
  variants use a different editor and aren't wired yet.
- Threading: the server writes only the mutex-protected `SharedScript` and marshals
  the editor update onto the message thread; it never calls the script engine
  directly (that stays on the GL thread) — same rule as the audio/video hosts.
- A screenshot can look blank if the app window is fully occluded (JUCE pauses the
  GL render when hidden) — keep it visible while grabbing.
