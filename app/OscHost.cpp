// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OscHost.h"
#include "FluxusCommands.h"

#include <juce_osc/juce_osc.h>

#include <cstdio>
#include <string>
#include <vector>

// A JUCE OSC receiver + sender. Received messages are stored (latest per address)
// in FluxusCommands; a bridge is installed so the script-facing osc-source /
// osc-destination / osc-send reach this transport. Numeric args only (float32 /
// int32) — enough for live control; sends go out as float32.
class JuceOscHost : public IOscHost,
                    private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback> {
public:
  void start() override {
    receiver.addListener(this);
    FluxOscBridge bridge;
    bridge.openSource     = [this](int port) { openSource(port); };
    bridge.setDestination = [this](const std::string& host, int port) { setDestination(host, port); };
    bridge.send           = [this](const std::string& addr, const std::vector<double>& args) { sendMessage(addr, args); };
    flux_osc_install_bridge(bridge);
  }
  void stop() override {
    flux_osc_install_bridge(FluxOscBridge{});   // detach the bridge first
    receiver.removeListener(this);
    receiver.disconnect();
    sender.disconnect();
  }

private:
  void openSource(int port) {
    receiver.disconnect();
    if (receiver.connect(port)) std::fprintf(stderr, "[osc] listening on port %d\n", port);
    else                        std::fprintf(stderr, "[osc] could not open port %d\n", port);
  }
  void setDestination(const std::string& host, int port) {
    destHost = host.empty() ? "127.0.0.1" : host;
    destPort = port;
    sender.disconnect();
    sender.connect(juce::String(destHost), destPort);
  }
  void sendMessage(const std::string& addr, const std::vector<double>& args) {
    if (destPort <= 0 || addr.empty()) return;
    juce::OSCMessage msg{ juce::OSCAddressPattern(juce::String::fromUTF8(addr.c_str())) };
    for (double a : args) msg.addFloat32((float) a);
    sender.send(msg);
  }
  void oscMessageReceived(const juce::OSCMessage& msg) override {
    std::vector<double> args;
    for (const auto& a : msg) {
      if      (a.isFloat32()) args.push_back(a.getFloat32());
      else if (a.isInt32())   args.push_back(a.getInt32());
    }
    flux_set_osc(msg.getAddressPattern().toString().toRawUTF8(), args.data(), (int) args.size());
  }

  juce::OSCReceiver receiver;
  juce::OSCSender   sender;
  std::string       destHost = "127.0.0.1";
  int               destPort = 0;
};

std::unique_ptr<IOscHost> makeJuceOscHost() { return std::make_unique<JuceOscHost>(); }
