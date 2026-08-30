// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

struct SharedScript;
namespace juce { class StreamingSocket; }

// Localhost control server for remote live-coding (drives the same buffer the
// editor does). A background thread accepts one-shot JSON line requests on
// 127.0.0.1:<port> and answers with one JSON line. The MCP server (mcp/) is a
// thin bridge that turns MCP tool calls into these requests.
//
// Threading: reads/writes only mutex-protected SharedScript + calls the load
// callback (which the component marshals onto the message thread). It NEVER calls
// the script engine directly (gotcha #1) — eval happens on the GL thread as usual.
class ControlServer {
public:
  // loadOnMsgThread: given new code, update the on-screen editor + run it (the
  // component wraps this in MessageManager::callAsync so it lands on the UI thread).
  ControlServer(SharedScript* shared,
                std::function<void(std::string)> loadOnMsgThread,
                int port);
  ~ControlServer();

private:
  void run();
  void handle(juce::StreamingSocket& conn);

  SharedScript* shared;
  std::function<void(std::string)> loadCb;
  int port;
  std::atomic<bool> running { true };
  std::unique_ptr<juce::StreamingSocket> listener;
  std::thread th;
};
