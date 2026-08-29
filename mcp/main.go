// fluxus-mcp — an MCP (stdio) server that drives a running Fluxus app's live-coding
// buffer. It bridges MCP tool calls to the app's localhost control server
// (app/ControlServer.cpp), which writes the same script buffer the editor does.
//
// Launch the app with the control port set, then run this server:
//   FLUXUS_CONTROL_PORT=8020 open build/FluxusRacketApp_artefacts/Release/FluxusRacketApp.app
//   FLUXUS_CONTROL_PORT=8020 ./fluxus-mcp        # (Claude Code spawns this via .mcp.json)
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"time"

	"github.com/mark3labs/mcp-go/mcp"
	"github.com/mark3labs/mcp-go/server"
)

// call sends one JSON request line to the app control server and reads one JSON
// response line back. Each call is a fresh connection (the server is one-shot).
func call(req map[string]any) (map[string]any, error) {
	port := os.Getenv("FLUXUS_CONTROL_PORT")
	if port == "" {
		port = "8020"
	}
	conn, err := net.DialTimeout("tcp", "127.0.0.1:"+port, 5*time.Second)
	if err != nil {
		return nil, fmt.Errorf("cannot reach Fluxus on 127.0.0.1:%s (is the app running with FLUXUS_CONTROL_PORT set?): %w", port, err)
	}
	defer conn.Close()
	b, _ := json.Marshal(req)
	if _, err := conn.Write(append(b, '\n')); err != nil {
		return nil, err
	}
	conn.SetReadDeadline(time.Now().Add(15 * time.Second))
	var resp map[string]any
	if err := json.NewDecoder(conn).Decode(&resp); err != nil {
		return nil, err
	}
	return resp, nil
}

func str(m map[string]any, k string) string {
	if v, ok := m[k].(string); ok {
		return v
	}
	return ""
}

func main() {
	s := server.NewMCPServer("fluxus-live", "0.1.0")

	s.AddTool(mcp.NewTool("fluxus_load",
		mcp.WithDescription("Replace the running Fluxus sketch with new Scheme code and evaluate it live. Returns the eval error if the code failed, else 'ok'. The on-screen editor updates too."),
		mcp.WithString("code", mcp.Required(), mcp.Description("Full sketch source (Scheme). Replaces the whole buffer."))),
		func(ctx context.Context, r mcp.CallToolRequest) (*mcp.CallToolResult, error) {
			code, _ := r.RequireString("code")
			resp, err := call(map[string]any{"cmd": "load", "code": code})
			if err != nil {
				return mcp.NewToolResultError(err.Error()), nil
			}
			if e := str(resp, "error"); e != "" {
				return mcp.NewToolResultText("eval error: " + e), nil
			}
			return mcp.NewToolResultText("ok"), nil
		})

	s.AddTool(mcp.NewTool("fluxus_get",
		mcp.WithDescription("Get the Fluxus sketch source currently running (read it, edit it, then fluxus_load the result).")),
		func(ctx context.Context, r mcp.CallToolRequest) (*mcp.CallToolResult, error) {
			resp, err := call(map[string]any{"cmd": "get"})
			if err != nil {
				return mcp.NewToolResultError(err.Error()), nil
			}
			return mcp.NewToolResultText(str(resp, "code")), nil
		})

	s.AddTool(mcp.NewTool("fluxus_error",
		mcp.WithDescription("Get the last evaluation error from the running sketch ('' = ok).")),
		func(ctx context.Context, r mcp.CallToolRequest) (*mcp.CallToolResult, error) {
			resp, err := call(map[string]any{"cmd": "error"})
			if err != nil {
				return mcp.NewToolResultError(err.Error()), nil
			}
			e := str(resp, "error")
			if e == "" {
				e = "(ok)"
			}
			return mcp.NewToolResultText(e), nil
		})

	s.AddTool(mcp.NewTool("fluxus_save",
		mcp.WithDescription("Save the running sketch to a .scm file on disk."),
		mcp.WithString("path", mcp.Required(), mcp.Description("Absolute path to write the sketch to."))),
		func(ctx context.Context, r mcp.CallToolRequest) (*mcp.CallToolResult, error) {
			path, _ := r.RequireString("path")
			resp, err := call(map[string]any{"cmd": "save", "path": path})
			if err != nil {
				return mcp.NewToolResultError(err.Error()), nil
			}
			if ok, _ := resp["ok"].(bool); !ok {
				return mcp.NewToolResultError("save failed for " + path), nil
			}
			return mcp.NewToolResultText("saved " + str(resp, "path")), nil
		})

	s.AddTool(mcp.NewTool("fluxus_screenshot",
		mcp.WithDescription("Grab the current frame of the running sketch to a PNG and return the file path (read the file to view it)."),
		mcp.WithString("path", mcp.Description("Optional PNG path; defaults to a temp file."))),
		func(ctx context.Context, r mcp.CallToolRequest) (*mcp.CallToolResult, error) {
			path := r.GetString("path", "")
			resp, err := call(map[string]any{"cmd": "screenshot", "path": path})
			if err != nil {
				return mcp.NewToolResultError(err.Error()), nil
			}
			return mcp.NewToolResultText("wrote " + str(resp, "path")), nil
		})

	if err := server.ServeStdio(s); err != nil {
		fmt.Fprintln(os.Stderr, "fluxus-mcp:", err)
		os.Exit(1)
	}
}
