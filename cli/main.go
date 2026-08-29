// fluxus — a CLI for the running Fluxus app's live-coding buffer. Talks to the
// app's localhost control server (app/ControlServer.cpp) over TCP, the same
// backend the MCP server uses. Launch the app with FLUXUS_CONTROL_PORT set:
//
//   FLUXUS_CONTROL_PORT=8020 open build/FluxusRacketApp_artefacts/Release/FluxusRacketApp.app
//
// Then:
//   fluxus load sketch.scm         # load + run a file (a '-' reads stdin)
//   fluxus eval '(clear)(build-cube)'
//   fluxus get > current.scm       # dump the running buffer
//   fluxus save out.scm            # app writes the running buffer to a file
//   fluxus error                   # last eval error
//   fluxus shot [out.png]          # grab a frame
//   fluxus watch sketch.scm        # reload on every save (live-code from any editor)
package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"time"
)

func port() string {
	if p := os.Getenv("FLUXUS_CONTROL_PORT"); p != "" {
		return p
	}
	return "8020"
}

// call sends one JSON request line and reads one JSON response line back.
func call(req map[string]any) (map[string]any, error) {
	conn, err := net.DialTimeout("tcp", "127.0.0.1:"+port(), 5*time.Second)
	if err != nil {
		return nil, fmt.Errorf("cannot reach Fluxus on 127.0.0.1:%s — is the app running with FLUXUS_CONTROL_PORT set? (%v)", port(), err)
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

func die(format string, a ...any) {
	fmt.Fprintf(os.Stderr, "fluxus: "+format+"\n", a...)
	os.Exit(1)
}

// loadSource sends code to the app; prints/returns the eval error (empty = ok).
func loadSource(code string) string {
	resp, err := call(map[string]any{"cmd": "load", "code": code})
	if err != nil {
		die("%v", err)
	}
	return str(resp, "error")
}

func readFileOrStdin(path string) string {
	if path == "-" {
		b, _ := io.ReadAll(os.Stdin)
		return string(b)
	}
	b, err := os.ReadFile(path)
	if err != nil {
		die("cannot read %s: %v", path, err)
	}
	return string(b)
}

func usage() {
	fmt.Fprint(os.Stderr, `fluxus — CLI for a running Fluxus sketch (control port via FLUXUS_CONTROL_PORT, default 8020)

  fluxus load <file|->        load + run a sketch file (or stdin)
  fluxus eval '<code>'        load + run inline code
  fluxus get                  print the running sketch source
  fluxus save <path>          app writes the running sketch to <path>
  fluxus error                print the last eval error
  fluxus shot [path]          grab a frame to PNG (prints the path)
  fluxus watch <file>         reload the file on every change (Ctrl+C to stop)
`)
	os.Exit(2)
}

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	args := os.Args[2:]
	switch os.Args[1] {

	case "load":
		if len(args) < 1 {
			die("load needs a file (or - for stdin)")
		}
		if e := loadSource(readFileOrStdin(args[0])); e != "" {
			die("%s", e)
		}
		fmt.Println("ok")

	case "eval":
		if len(args) < 1 {
			die("eval needs code")
		}
		if e := loadSource(args[0]); e != "" {
			die("%s", e)
		}
		fmt.Println("ok")

	case "get":
		resp, err := call(map[string]any{"cmd": "get"})
		if err != nil {
			die("%v", err)
		}
		fmt.Print(str(resp, "code"))

	case "save":
		if len(args) < 1 {
			die("save needs a path")
		}
		resp, err := call(map[string]any{"cmd": "save", "path": args[0]})
		if err != nil {
			die("%v", err)
		}
		if ok, _ := resp["ok"].(bool); !ok {
			die("save failed for %s", args[0])
		}
		fmt.Println("saved", str(resp, "path"))

	case "error":
		resp, err := call(map[string]any{"cmd": "error"})
		if err != nil {
			die("%v", err)
		}
		if e := str(resp, "error"); e != "" {
			fmt.Println(e)
		} else {
			fmt.Println("(ok)")
		}

	case "shot", "screenshot":
		path := ""
		if len(args) > 0 {
			path = args[0]
		}
		resp, err := call(map[string]any{"cmd": "screenshot", "path": path})
		if err != nil {
			die("%v", err)
		}
		fmt.Println(str(resp, "path"))

	case "watch":
		if len(args) < 1 {
			die("watch needs a file")
		}
		watch(args[0])

	default:
		usage()
	}
}

// watch reloads the file whenever its mtime changes (poll — no external deps).
func watch(path string) {
	fmt.Fprintf(os.Stderr, "watching %s — reloading on change (Ctrl+C to stop)\n", path)
	var last time.Time
	first := true
	for {
		fi, err := os.Stat(path)
		if err == nil && fi.ModTime() != last {
			last = fi.ModTime()
			if !first {
				time.Sleep(40 * time.Millisecond) // let the editor finish writing
			}
			first = false
			e := loadSource(readFileOrStdin(path))
			ts := time.Now().Format("15:04:05")
			if e == "" {
				fmt.Printf("[%s] reloaded %s — ok\n", ts, path)
			} else {
				fmt.Printf("[%s] reloaded %s — ERROR: %s\n", ts, path, e)
			}
		}
		time.Sleep(250 * time.Millisecond)
	}
}
