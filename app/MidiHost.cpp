#include "MidiHost.h"
#include "FluxusCommands.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <cstdio>
#include <vector>

// Opens every available MIDI input and forwards controller/note messages into
// FluxusCommands. Note-off is stored as velocity 0 on the same note.
class JuceMidiHost : public IMidiHost,
                     private juce::MidiInputCallback {
public:
  void start() override {
    const auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& d : devices) {
      if (auto in = juce::MidiInput::openDevice(d.identifier, this)) {
        in->start();
        std::fprintf(stderr, "[midi] input: %s\n", d.name.toRawUTF8());
        inputs.push_back(std::move(in));
      }
    }
    if (inputs.empty()) std::fprintf(stderr, "[midi] no MIDI inputs found\n");
  }
  void stop() override {
    for (auto& in : inputs) in->stop();
    inputs.clear();
  }

private:
  void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
    if (m.isController())
      flux_set_midi_cc(m.getChannel() - 1, m.getControllerNumber(), m.getControllerValue());
    else if (m.isNoteOn())
      flux_set_midi_note(m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
      flux_set_midi_note(m.getNoteNumber(), 0);
  }

  std::vector<std::unique_ptr<juce::MidiInput>> inputs;
};

std::unique_ptr<IMidiHost> makeJuceMidiHost() { return std::make_unique<JuceMidiHost>(); }
