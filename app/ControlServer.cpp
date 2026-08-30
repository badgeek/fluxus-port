// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ControlServer.h"
#include "SharedScript.h"
#include "FluxusCommands.h"      // flux_screenshot (thread-safe request)

#include <juce_core/juce_core.h>

#include <chrono>
#include <thread>

using juce::StreamingSocket;

ControlServer::ControlServer(SharedScript* s,
                             std::function<void(std::string)> loadOnMsgThread,
                             int p)
  : shared(s), loadCb(std::move(loadOnMsgThread)), port(p) {
  th = std::thread([this] { run(); });
}

ControlServer::~ControlServer() {
  running = false;
  if (listener) listener->close();       // unblocks waitForNextConnection()
  if (th.joinable()) th.join();
}

void ControlServer::run() {
  listener = std::make_unique<StreamingSocket>();
  if (!listener->createListener(port, "127.0.0.1")) {
    std::fprintf(stderr, "[control] could not listen on 127.0.0.1:%d\n", port);
    return;
  }
  std::fprintf(stderr, "[control] live-coding control on 127.0.0.1:%d\n", port);
  while (running) {
    std::unique_ptr<StreamingSocket> conn(listener->waitForNextConnection());
    if (!conn) continue;                 // closed (shutdown) or error
    handle(*conn);
  }
}

// read one newline-terminated line (blocking); returns false if the peer closed.
static bool readLine(StreamingSocket& c, std::string& out) {
  out.clear();
  char ch;
  for (;;) {
    const int n = c.read(&ch, 1, true);
    if (n <= 0) return !out.empty();
    if (ch == '\n') return true;
    if (ch != '\r') out += ch;
    if (out.size() > (size_t) 8 * 1024 * 1024) return true;   // sanity cap
  }
}

void ControlServer::handle(StreamingSocket& conn) {
  std::string line;
  if (!readLine(conn, line)) return;

  const juce::var req = juce::JSON::parse(juce::String::fromUTF8(line.c_str()));
  const juce::String cmd = req.getProperty("cmd", juce::var()).toString();

  juce::DynamicObject::Ptr resp = new juce::DynamicObject();
  resp->setProperty("ok", true);

  if (cmd == "load") {
    const std::string code = req.getProperty("code", juce::var()).toString().toStdString();
    if (loadCb) loadCb(code);
    // give the GL thread a couple of frames to eval, then report any error
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string err;
    { std::lock_guard<std::mutex> lk(shared->m); err = shared->lastError; }
    resp->setProperty("error", juce::String(err));
  } else if (cmd == "get") {
    std::string code;
    { std::lock_guard<std::mutex> lk(shared->m); code = shared->pending; }
    resp->setProperty("code", juce::String::fromUTF8(code.c_str()));
  } else if (cmd == "error") {
    std::string err;
    { std::lock_guard<std::mutex> lk(shared->m); err = shared->lastError; }
    resp->setProperty("error", juce::String(err));
  } else if (cmd == "save") {
    const juce::String path = req.getProperty("path", juce::var()).toString();
    std::string code;
    { std::lock_guard<std::mutex> lk(shared->m); code = shared->pending; }
    juce::File f(path);
    const bool wrote = path.isNotEmpty() && f.replaceWithText(juce::String::fromUTF8(code.c_str()));
    resp->setProperty("ok", wrote);
    resp->setProperty("path", path);
  } else if (cmd == "screenshot") {
    juce::String path = req.getProperty("path", juce::var()).toString();
    if (path.isEmpty())
      path = juce::File::getSpecialLocation(juce::File::tempDirectory)
               .getChildFile("fluxus-mcp-shot.png").getFullPathName();
    flux_screenshot(path.toRawUTF8());   // GL thread grabs the finished frame -> PNG
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    resp->setProperty("path", path);
  } else {
    resp->setProperty("ok", false);
    resp->setProperty("error", "unknown cmd");
  }

  juce::String out = juce::JSON::toString(juce::var(resp.get()), true) + "\n";
  const juce::MemoryBlock mb = out.toUTF8().getAddress()
      ? juce::MemoryBlock(out.toRawUTF8(), out.getNumBytesAsUTF8()) : juce::MemoryBlock();
  conn.write(mb.getData(), (int) mb.getSize());
}
