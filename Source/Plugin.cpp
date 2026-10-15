#include "Dsp.h"
#include "LegacyParamOrder.h"
#include "ModCurve.h"
#include "Preset.h"
#include <memory>
#include <regex>
using namespace juce;
using namespace aurora;
#ifndef AURORA_FX
#define AURORA_FX 0
#endif
static const String &key(const char *prefix, int i, const char *suffix) {
    struct Keys {
        const char *prefixes[9]{"osc",   "env", "lfo",  "macro", "filter",
                                "route", "mod", "step", "fx"};
        const char *suffixes[9][32]{
            {"mode", "level", "tune", "position", "pan", "grain", "loop", "unison", "detune",
             "warp", "warp_amount", "phase", "random"},
            {"a",  "d",  "s",  "r",  "mode", "count", "tension", "duration", "loop",
             "x0", "x1", "x2", "x3", "x4",   "x5",    "x6",      "x7",       "y0",
             "y1", "y2", "y3", "y4", "y5",   "y6",    "y7"},
            {"rate",   "shape",   "sync",   "division", "phase",  "point0", "point1",
             "point2", "point3",  "point4", "point5",   "point6", "point7", "x0",
             "x1",     "x2",      "x3",     "x4",       "x5",     "x6",     "x7",
             "count",  "tension", "delay",  "rise"},
            {"value"},
            {"cutoff", "res", "type"},
            {"balance", "bus1", "bus2", "dest", "env"},
            {"source", "target", "depth", "curve"},
            {"note", "on"},
            {"type", "amount", "mix", "time"}};
        int counts[9]{3, 4, 10, 8, 2, 7, 64, 16, 12};
        std::array<std::array<std::array<String, 32>, 64>, 9> ids;
        Keys() {
            for (int family = 0; family < 9; ++family)
                for (int instance = 0; instance < counts[family]; ++instance)
                    for (int s = 0; s < 32 && suffixes[family][s]; ++s)
                        ids[size_t(family)][size_t(instance)][size_t(s)] =
                            (family == 8 ? "fx" + String(instance / 4) + "s" + String(instance % 4)
                                         : String(prefixes[family]) + String(instance)) +
                            "_" + suffixes[family][s];
        }
    };
    static const Keys cache;
    for (int family = 0; family < 9; ++family)
        if (std::strcmp(prefix, cache.prefixes[family]) == 0 && i >= 0 && i < cache.counts[family])
            for (int s = 0; s < 32 && cache.suffixes[family][s]; ++s)
                if (std::strcmp(suffix, cache.suffixes[family][s]) == 0)
                    return cache.ids[size_t(family)][size_t(i)][size_t(s)];
    static const String empty;
    return empty;
}
class Aurora;
struct Zone {
    AudioBuffer<float> data;
    double sampleRate = 48000;
    int root = 60, low = 0, high = 127;
};
struct Sound : SynthesiserSound {
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};
class Voice : public SynthesiserVoice {
  public:
    explicit Voice(Aurora &p) : owner(p) {}
    bool canPlaySound(SynthesiserSound *s) override { return dynamic_cast<Sound *>(s) != nullptr; }
    void startNote(int, float, SynthesiserSound *, int) override;
    void stopNote(float, bool) override;
    void pitchWheelMoved(int v) override { bend = std::pow(2., ((v - 8192) / 8192.) * 2. / 12.); }
    void controllerMoved(int, int) override {}
    void aftertouchChanged(int v) override { pressure = v / 127.f; }
    void channelPressureChanged(int v) override { pressure = v / 127.f; }
    void renderNextBlock(AudioBuffer<float> &, int, int) override;
    Aurora &owner;
    std::array<ADSR, 4> envs;
    std::array<double, 3> phase{}, position{};
    std::array<std::array<double, 8>, 3> unisonPhase{};
    std::array<double, 4> envelopeTime{};
    bool keepEnvelope = false;
    float glideNote = 60;
    std::array<int, 3> zone{};
    double subphase = 0, bend = 1;
    float velocity = 0, pressure = 0;
    int note = 60;
    bool released = false;
    Random rng;
};
class PerformanceSynth : public Synthesiser {
  public:
    int mode = 0, limit = 16;
    int serial = 0;
    std::array<int, 2048> heldKeys{};
    std::array<float, 2048> velocities{};
    void noteOn(int channel, int note, float velocity) override {
        int id = (channel - 1) * 128 + note;
        heldKeys[size_t(id)] = ++serial;
        velocities[size_t(id)] = velocity;
        if (mode == 2)
            if (auto *voice = dynamic_cast<Voice *>(getVoice(0)))
                voice->keepEnvelope = voice->isVoiceActive();
        if (mode) {
            auto *voice = dynamic_cast<Voice *>(getVoice(0));
            voice->keepEnvelope = mode == 2 && voice->isVoiceActive() && voice->isKeyDown();
            startVoice(voice, getSound(0).get(), channel, note, velocity);
        } else
            Synthesiser::noteOn(channel, note, velocity);
    }
    void noteOff(int channel, int note, float velocity, bool tail) override {
        heldKeys[size_t((channel - 1) * 128 + note)] = 0;
        if (mode && getVoice(0)->getCurrentlyPlayingNote() == note &&
            getVoice(0)->isPlayingChannel(channel)) {
            int latest = -1;
            for (int i = 0; i < 2048; ++i)
                if (heldKeys[size_t(i)] &&
                    (latest < 0 || heldKeys[size_t(i)] > heldKeys[size_t(latest)]))
                    latest = i;
            if (latest >= 0) {
                noteOn(latest / 128 + 1, latest % 128, velocities[size_t(latest)]);
                return;
            }
        }
        Synthesiser::noteOff(channel, note, velocity, tail);
    }
    void allNotesOff(int channel, bool tail) override {
        heldKeys.fill(0);
        Synthesiser::allNotesOff(channel, tail);
    }

  protected:
    void handleMidiEvent(const MidiMessage &message) override {
        if (message.isAllNotesOff() || message.isAllSoundOff())
            heldKeys.fill(0);
        Synthesiser::handleMidiEvent(message);
    }
    SynthesiserVoice *findFreeVoice(SynthesiserSound *sound, int, int, bool steal) const override {
        SynthesiserVoice *oldest = nullptr;
        for (int i = 0; i < limit; ++i) {
            auto *voice = getVoice(i);
            if (!voice->isVoiceActive() && voice->canPlaySound(sound))
                return voice;
            if (!oldest || voice->wasStartedBefore(*oldest))
                oldest = voice;
        }
        return steal ? oldest : nullptr;
    }
};
class Aurora : public AudioProcessor {
  public:
    UndoManager undo;
    AudioProcessorValueTreeState state;
    WaveTap telemetry;
    std::atomic<int> midiLearn{-1};
    std::array<std::atomic<int>, 128> midiMappings;
    PerformanceSynth synth;
    std::array<DisplayQueue, 3> displayFrames;
    int displayClock = 0;
    std::array<CurveShape, 4> envCurves;
    std::array<CurveShape, 10> lfoCurves;
    std::array<double, 10> lfoTime{};
    float previousNote = 60;
    Wavetables tables;
    MidiKeyboardState keyboard;
    MidiBuffer midiScratch, sequenceOutput;
    std::atomic<int> uiPitch{8192}, uiMod{0};
    std::atomic<float> modwheel{0};
    int lastUiPitch = 8192, lastUiMod = 0;
    std::array<MemoryBlock, 3> irData;
    std::array<std::vector<Zone>, 3> samples;
    std::array<std::array<float, 64>, 3> spectrum{};
    std::array<std::array<Effect, 4>, 3> fx;
    std::array<std::array<SVF, 2>, 2> filters;
    std::array<AudioBuffer<float>, 4> buses;
    std::array<double, 10> lfoPhase{};
    std::array<float, 10> lfoValue{};
    AudioFormatManager formats;
    std::vector<std::atomic<float> *> raw;
    std::vector<float> snapshot;
    std::vector<bool> smoothParameters;
    std::map<String, int> indices;
    struct RouteConfig {
        float balance = 0, bus1 = 0, bus2 = 0;
        int dest = 0;
        bool env = true;
    };
    std::array<RouteConfig, 7> routes;
    struct ModConfig {
        int source = 0, target = 0;
        float depth = 0, curve = 1;
    };
    std::array<ModConfig, 64> mods;
    std::array<float, 8> macros{};
    AudioBuffer<float> cutoffMod;
    double rate = 48000;
    int maxBlock = 512;
    float gainSmooth = .6f;
    // 0..127 + generated clip; arpeggiator retains MIDI input note set.
    std::array<bool, 128> held{};
    double stepClock = 0;
    int step = 0, lastSequenceNote = -1;
    Aurora()
        : AudioProcessor(
              AURORA_FX ? BusesProperties()
                              .withInput("Stereo", AudioChannelSet::stereo(), true)
                              .withOutput("Stereo", AudioChannelSet::stereo(), true)
                        : BusesProperties().withOutput("Stereo", AudioChannelSet::stereo(), true)),
          state(*this, &undo, "AuroraState", layout()) {
        for (auto &m : midiMappings)
            m.store(-1);
        formats.registerBasicFormats();
        for (int i = 0; i < 16; ++i)
            synth.addVoice(new Voice(*this));
        synth.addSound(new Sound);
        for (auto *p : getParameters())
            if (auto *rp = dynamic_cast<RangedAudioParameter *>(p)) {
                indices[rp->paramID] = int(raw.size());
                raw.push_back(state.getRawParameterValue(rp->paramID));
                smoothParameters.push_back(dynamic_cast<AudioParameterFloat *>(rp) != nullptr &&
                                           !rp->paramID.endsWith("_dest") &&
                                           !rp->paramID.endsWith("_env") &&
                                           !rp->paramID.startsWith("step"));
            }
        snapshot.resize(raw.size());
        capture(true);
        for (auto &s : spectrum) {
            s.fill(0);
            s[0] = 1;
        }
    }
    static AudioProcessorValueTreeState::ParameterLayout layout() {
        AudioProcessorValueTreeState::ParameterLayout p;
        auto f = [&](String id, String name, float lo, float hi, float def, float skew = 1.f) {
            NormalisableRange<float> r(lo, hi);
            r.skew = skew;
            if (id.endsWith("_count") || id.endsWith("_unison") || id == "voice_limit" ||
                id == "transpose" || id == "arp_octaves")
                r.interval = 1;
            p.add(std::make_unique<AudioParameterFloat>(ParameterID(id, 1), name, r, def));
        };
        auto choice = [&](String id, String name, StringArray choices, int def = 0) {
            p.add(std::make_unique<AudioParameterChoice>(ParameterID(id, 1), name, choices, def));
        };
        auto boolean = [&](String id, String name, bool def) {
            p.add(std::make_unique<AudioParameterBool>(ParameterID(id, 1), name, def));
        };
        f("master", "Master", 0, 1, AURORA_FX ? 1.f : .6f);
        f("noise", "Noise", 0, 1, 0);
        f("sub", "Sub", 0, 1, .15f);
        for (int i = 0; i < 3; ++i) {
            String n = "Osc " + String(i + 1) + " ";
            choice(key("osc", i, "mode"), n + "engine",
                   {"Wavetable", "Sample", "Multisample", "Granular", "Spectral"});
            f(key("osc", i, "level"), n + "level", 0, 1, i == 0 ? .7f : 0);
            f(key("osc", i, "tune"), n + "semitones", -24, 24, 0);
            f(key("osc", i, "position"), n + "table / grain position", 0, 1, 0);
            f(key("osc", i, "pan"), n + "pan", -1, 1, 0);
            f(key("osc", i, "grain"), n + "grain length ms", 10, 250, 80);
            boolean(key("osc", i, "loop"), n + "sample loop", true);
            f(key("osc", i, "unison"), n + "unison voices", 1, 8, 1);
            f(key("osc", i, "detune"), n + "detune cents", 0, 50, 0);
            choice(key("osc", i, "warp"), n + "warp", {"Off", "Bend", "Sync", "Fold"});
            f(key("osc", i, "warp_amount"), n + "warp amount", 0, 1, .5f);
            f(key("osc", i, "phase"), n + "phase", 0, 1, 0);
            f(key("osc", i, "random"), n + "random phase", 0, 1, 0);
        }
        for (int i = 0; i < 4; ++i) {
            String n = "Env " + String(i + 1) + " ";
            f(key("env", i, "a"), n + "attack", .001f, 5, .01f, .4f);
            f(key("env", i, "d"), n + "decay", .001f, 5, .2f, .4f);
            f(key("env", i, "s"), n + "sustain", 0, 1, .8f);
            f(key("env", i, "r"), n + "release", .005f, 10, .3f, .4f);
            choice(key("env", i, "mode"), n + "editor mode", {"ADSR", "Curve"});
            f(key("env", i, "count"), n + "points", 2, 8, 8);
            f(key("env", i, "tension"), n + "Bezier tension", -1, 1, 0);
            f(key("env", i, "duration"), n + "curve duration", .03f, 12, 1, .4f);
            boolean(key("env", i, "loop"), n + "curve loop", false);
            const float ys[8]{0, 1, .8f, .65f, .5f, .3f, .15f, 0};
            for (int j = 0; j < 8; ++j) {
                f(key("env", i, ("x" + String(j)).toRawUTF8()), n + "point X", 0, 1, j / 7.f);
                f(key("env", i, ("y" + String(j)).toRawUTF8()), n + "point Y", 0, 1, ys[j]);
            }
        }
        for (int i = 0; i < 10; ++i) {
            f(key("lfo", i, "rate"), "LFO " + String(i + 1) + " Hz", .01f, 30, 1, .4f);
            choice(key("lfo", i, "shape"), "LFO shape",
                   {"Sine", "Triangle", "Saw", "Square", "Custom"});
            boolean(key("lfo", i, "sync"), "LFO tempo sync", false);
            choice(key("lfo", i, "division"), "LFO division",
                   {"4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16"}, 4);
            f(key("lfo", i, "phase"), "LFO phase", 0, 1, 0);
            for (int j = 0; j < 8; ++j)
                f(key("lfo", i, ("point" + String(j)).toRawUTF8()), "LFO point", 0, 1,
                  .5f + .5f * std::sin(2 * pi * j / 7.f));
            for (int j = 0; j < 8; ++j)
                f(key("lfo", i, ("x" + String(j)).toRawUTF8()), "LFO point X", 0, 1, j / 7.f);
            f(key("lfo", i, "count"), "LFO points", 2, 8, 8);
            f(key("lfo", i, "tension"), "LFO Bezier tension", -1, 1, 0);
            f(key("lfo", i, "delay"), "LFO delay seconds", 0, 5, 0);
            f(key("lfo", i, "rise"), "LFO rise seconds", 0, 5, 0);
        }
        for (int i = 0; i < 8; ++i)
            f(key("macro", i, "value"), "Macro " + String(i + 1), 0, 1, 0);
        for (int i = 0; i < 2; ++i) {
            f(key("filter", i, "cutoff"), "Filter " + String(i + 1) + " Hz", 20, 20000, 16000, .3f);
            f(key("filter", i, "res"), "Resonance", 0, 1, .1f);
            choice(key("filter", i, "type"), "Filter type", {"Low pass", "Band pass", "High pass"});
        }
        for (int i = 0; i < 7; ++i) {
            String n = "Route " + String(i + 1) + " ";
            f(key("route", i, "balance"), n + "filter balance", -100, 100, 0);
            f(key("route", i, "bus1"), n + "FX bus 1", 0, 100, 0);
            f(key("route", i, "bus2"), n + "FX bus 2", 0, 100, 0);
            // Raw 0..1 preserves the exact thresholds from the supplied descriptors.
            f(key("route", i, "dest"), n + "destination [0 Filter, .25 Main, .5 Direct, .75 None]",
              0, 1, i < 5 && !AURORA_FX ? 0 : .25f);
            f(key("route", i, "env"), n + "Via Env1 [.5 = on]", 0, 1, i < 5 ? 1 : 0);
        }
        const StringArray types{"Off",          "Distortion",    "Flanger",    "Phaser",
                                "Chorus",       "Delay",         "Compressor", "Reverb",
                                "EQ",           "Filter",        "Hyper",      "Bode approximation",
                                "Convolution",  "Utility width", "Dimension",  "Mid/side",
                                "2 band split", "3 band split"};
        for (int r = 0; r < 3; ++r)
            for (int j = 0; j < 4; ++j) {
                String id = "fx" + String(r) + "s" + String(j) + "_",
                       n = "Rack " + String(r + 1) + " slot " + String(j + 1) + " ";
                choice(id + "type", n + "effect", types);
                f(id + "amount", n + "amount", 0, 1, .4f);
                f(id + "mix", n + "mix", 0, 1, .35f);
                f(id + "time", n + "time / cutoff", 0, 1, .2f);
            }
        StringArray sources{"Off", "Velocity", "Note", "Pressure"};
        for (int i = 0; i < 4; ++i)
            sources.add("Env " + String(i + 1));
        for (int i = 0; i < 10; ++i)
            sources.add("LFO " + String(i + 1));
        for (int i = 0; i < 8; ++i)
            sources.add("Macro " + String(i + 1));
        sources.add("Mod wheel");
        StringArray targets{"Off",
                            "Osc 1 level",
                            "Osc 2 level",
                            "Osc 3 level",
                            "Osc 1 position",
                            "Osc 2 position",
                            "Osc 3 position",
                            "Pitch",
                            "Pan",
                            "Filter 1 cutoff",
                            "Filter 2 cutoff"};
        for (int i = 0; i < 64; ++i) {
            choice(key("mod", i, "source"), "Mod source", sources);
            choice(key("mod", i, "target"), "Mod target", targets);
            f(key("mod", i, "depth"), "Mod depth", -1, 1, 0);
            f(key("mod", i, "curve"), "Mod curve", .25f, 4, 1);
        }
        choice("seq_mode", "Sequencer", {"Off", "Arpeggiator", "Clip"});
        f("seq_bpm", "Fallback BPM", 30, 240, 120);
        choice("seq_div", "Step division", {"1/4", "1/8", "1/16", "1/32"}, 2);
        f("seq_gate", "Gate", .05f, .95f, .75f);
        choice("voice_mode", "Voicing", {"Poly", "Mono", "Legato"});
        f("voice_limit", "Voice count", 1, 16, 16);
        f("portamento", "Portamento ms", 0, 500, 0);
        boolean("porta_always", "Portamento always", false);
        f("transpose", "Transpose semitones", -24, 24, 0);
        choice("key", "Key", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
        choice("scale", "Scale", {"Chromatic", "Major", "Minor"});
        choice("arp_order", "Arp direction", {"Up", "Down", "Up/Down", "Random"});
        f("arp_octaves", "Arp octaves", 1, 4, 1);
        f("arp_swing", "Arp swing", 0, .5f, 0);
        for (int i = 0; i < 16; ++i) {
            f(key("step", i, "note"), "Step " + String(i + 1) + " note", 0, 127,
              60 + ((i % 4) * 2));
            boolean(key("step", i, "on"), "Step enabled", i % 2 == 0);
        }
        return p;
    }
    void capture(bool instant = false, int block = 0) {
        float blend =
            instant
                ? 1.f
                : float(1 - std::exp(-std::max(1, block > 0 ? block : maxBlock) / (rate * .015)));
        for (size_t i = 0; i < raw.size(); ++i) {
            float value = raw[i]->load();
            snapshot[i] = smoothParameters[i] ? snapshot[i] + (value - snapshot[i]) * blend : value;
        }
        for (int j = 0; j < 7; ++j)
            routes[size_t(j)] = {(val(key("route", j, "balance")) + 100) / 200,
                                 val(key("route", j, "bus1")) * .01f,
                                 val(key("route", j, "bus2")) * .01f,
                                 std::min(3, int(val(key("route", j, "dest")) * 4)),
                                 val(key("route", j, "env")) >= .5f};
        for (int j = 0; j < 64; ++j)
            mods[size_t(j)] = {int(val(key("mod", j, "source"))), int(val(key("mod", j, "target"))),
                               val(key("mod", j, "depth")), val(key("mod", j, "curve"))};
        for (int j = 0; j < 8; ++j)
            macros[size_t(j)] = val(key("macro", j, "value"));
        static const char *xs[]{"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
        static const char *ys[]{"y0", "y1", "y2", "y3", "y4", "y5", "y6", "y7"};
        static const char *ps[]{"point0", "point1", "point2", "point3",
                                "point4", "point5", "point6", "point7"};
        for (int j = 0; j < 4; ++j) {
            auto &curve = envCurves[size_t(j)];
            curve.count = juce::jlimit(2, 8, int(val(key("env", j, "count"))));
            curve.tension = val(key("env", j, "tension"));
            for (int i = 0; i < 8; ++i) {
                curve.x[size_t(i)] = val(key("env", j, xs[i]));
                curve.y[size_t(i)] = val(key("env", j, ys[i]));
            }
        }
        for (int j = 0; j < 10; ++j) {
            auto &curve = lfoCurves[size_t(j)];
            curve.count = juce::jlimit(2, 8, int(val(key("lfo", j, "count"))));
            curve.tension = val(key("lfo", j, "tension"));
            for (int i = 0; i < 8; ++i) {
                curve.x[size_t(i)] = val(key("lfo", j, xs[i]));
                curve.y[size_t(i)] = val(key("lfo", j, ps[i]));
            }
        }
    }
    int pitchFor(int input) const {
        int note = juce::jlimit(0, 127, input + int(val("transpose"))), scale = int(val("scale")),
            root = int(val("key"));
        if (!scale)
            return note;
        static const int major[]{0, 2, 4, 5, 7, 9, 11}, minor[]{0, 2, 3, 5, 7, 8, 10};
        for (int distance = 0; distance < 12; ++distance)
            for (int sign : {-1, 1}) {
                int candidate = juce::jlimit(0, 127, note + distance * sign),
                    pc = (candidate - root + 120) % 12;
                for (int i = 0; i < 7; ++i)
                    if (pc == (scale == 1 ? major[i] : minor[i]))
                        return candidate;
            }
        return note;
    }
    float val(const String &id) const {
        auto it = indices.find(id);
        return it == indices.end() ? 0 : snapshot[size_t(it->second)];
    }
    void prepareToPlay(double sr, int block) override {
        rate = sr;
        capture(true);
        midiScratch.ensureSize(16384);
        sequenceOutput.ensureSize(16384);
        maxBlock = std::max(1, block);
        synth.setCurrentPlaybackSampleRate(sr);
        for (auto &b : buses)
            b.setSize(2, maxBlock);
        send1.setSize(2, maxBlock);
        send2.setSize(2, maxBlock);
        direct.setSize(2, maxBlock);
        cutoffMod.setSize(3, maxBlock);
        for (auto &r : fx)
            for (auto &e : r)
                e.prepare(sr, maxBlock);
        filters = {};
        lfoPhase = {};
        lfoTime = {};
        displayClock = 0;
        held = {};
        stepClock = 0;
        lastSequenceNote = -1;
        gainSmooth = val("master");
    }
    void releaseResources() override { synth.allNotesOff(0, false); }
    bool isBusesLayoutSupported(const BusesLayout &l) const override {
        return l.getMainOutputChannelSet() == AudioChannelSet::stereo() &&
               (!AURORA_FX || l.getMainInputChannelSet() == AudioChannelSet::stereo());
    }
    void route(int index, float l, float r, float env, int sample) {
        const auto &config = routes[size_t(index)];
        if (config.env) {
            l *= env;
            r *= env;
        }
        const int dest = config.dest;
        if (dest == 3)
            return;
        if (dest == 0 && index < 5) {
            float b = config.balance;
            for (int c = 0; c < 2; ++c) {
                float x = c == 0 ? l : r;
                buses[2].addSample(c, sample, x * (1 - b));
                buses[3].addSample(c, sample, x * b);
            }
        } else {
            buses[0].addSample(0, sample, l);
            buses[0].addSample(1, sample, r);
        }
        float a = config.bus1, b = config.bus2;
        // Dedicated parallel racks accumulate in output buffer slots 2/3 below via send buffers.
        send1.addSample(0, sample, l * a);
        send1.addSample(1, sample, r * a);
        send2.addSample(0, sample, l * b);
        send2.addSample(1, sample, r * b);
        if (dest == 2) {
            direct.addSample(0, sample, l);
            direct.addSample(1, sample, r);
            buses[0].addSample(0, sample, -l);
            buses[0].addSample(1, sample, -r);
        }
    }
    AudioBuffer<float> send1, send2, direct;
    void sequence(MidiBuffer &midi, int n) {
        int mode = int(val("seq_mode"));
        if (mode == 0) {
            if (lastSequenceNote >= 0) {
                midi.addEvent(MidiMessage::noteOff(1, lastSequenceNote), 0);
                lastSequenceNote = -1;
            }
            return;
        }
        auto &out = sequenceOutput;
        out.clear();
        for (const auto m : midi) {
            auto msg = m.getMessage();
            if (msg.isNoteOn())
                held[size_t(msg.getNoteNumber())] = true;
            else if (msg.isNoteOff())
                held[size_t(msg.getNoteNumber())] = false;
            else {
                out.addEvent(msg, m.samplePosition);
                if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    held = {};
            }
        }
        double bpm = val("seq_bpm");
        bool running = true;
        if (auto *ph = getPlayHead())
            if (auto pos = ph->getPosition()) {
                if (auto b = pos->getBpm())
                    bpm = *b;
                running = pos->getIsPlaying();
            }
        double length = rate * 60 / std::max(1., bpm) / std::pow(2., int(val("seq_div")));
        for (int i = 0; i < n; ++i) {
            if (!running) {
                if (lastSequenceNote >= 0) {
                    out.addEvent(MidiMessage::noteOff(1, lastSequenceNote), i);
                    lastSequenceNote = -1;
                }
                continue;
            }
            if (stepClock <= 0) {
                if (lastSequenceNote >= 0)
                    out.addEvent(MidiMessage::noteOff(1, lastSequenceNote), i);
                lastSequenceNote = -1;
                if (mode == 2 && val(key("step", step % 16, "on")) > .5f)
                    lastSequenceNote = int(val(key("step", step % 16, "note")));
                if (mode == 1) {
                    std::array<int, 512> notes{};
                    int count = 0;
                    for (int octave = 0; octave < int(val("arp_octaves")); ++octave)
                        for (int j = 0; j < 128; ++j)
                            if (held[size_t(j)] && j + octave * 12 < 128)
                                notes[size_t(count++)] = j + octave * 12;
                    if (count) {
                        int order = int(val("arp_order")), index = step % count;
                        if (order == 1)
                            index = count - 1 - index;
                        if (order == 2 && count > 1) {
                            index = step % (count * 2 - 2);
                            if (index >= count)
                                index = count * 2 - 2 - index;
                        }
                        if (order == 3)
                            index = (step * 1103515245u + 12345u) % unsigned(count);
                        lastSequenceNote = notes[size_t(index)];
                    }
                }
                if (lastSequenceNote >= 0)
                    out.addEvent(MidiMessage::noteOn(1, lastSequenceNote, .8f), i);
                step = (step + 1) % 1024;
                stepClock += length * (1 + (step % 2 ? 1 : -1) * val("arp_swing"));
            }
            if (lastSequenceNote >= 0 && stepClock < length * (1 - val("seq_gate"))) {
                out.addEvent(MidiMessage::noteOff(1, lastSequenceNote), i);
                lastSequenceNote = -1;
            }
            stepClock -= 1;
        }
        midi.swapWith(out);
    }
    void processBlock(AudioBuffer<float> &output, MidiBuffer &midi) override {
        ScopedNoDenormals denorm;
        if (!AURORA_FX) {
            keyboard.processNextMidiBuffer(midi, 0, output.getNumSamples(), true);
            int pitch = uiPitch.load(), wheel = uiMod.load();
            if (pitch != lastUiPitch) {
                midi.addEvent(MidiMessage::pitchWheel(1, pitch), 0);
                lastUiPitch = pitch;
            }
            if (wheel != lastUiMod) {
                midi.addEvent(MidiMessage::controllerEvent(1, 1, wheel), 0);
                lastUiMod = wheel;
            }
        }
        for (const auto event : midi) {
            auto msg = event.getMessage();
            if (msg.isController() && msg.getControllerNumber() < 120) {
                int cc = msg.getControllerNumber();
                if (cc == 1)
                    modwheel.store(msg.getControllerValue() / 127.f);
                int armed = midiLearn.exchange(-1);
                if (armed >= 0)
                    midiMappings[size_t(cc)].store(armed);
                int index = midiMappings[size_t(cc)].load();
                if (index >= 0 && index < getParameters().size())
                    getParameters()[index]->setValueNotifyingHost(msg.getControllerValue() / 127.f);
            }
        }
        capture(false, output.getNumSamples());
        int limit = int(val("voice_mode")) == 0 ? juce::jlimit(1, 16, int(val("voice_limit"))) : 1;
        if (limit < synth.limit)
            for (int i = limit; i < synth.limit; ++i)
                synth.getVoice(i)->stopNote(0, false);
        synth.limit = limit;
        synth.mode = int(val("voice_mode"));
        double bpm = val("seq_bpm");
        if (auto *playhead = getPlayHead())
            if (auto position = playhead->getPosition())
                if (auto tempo = position->getBpm())
                    bpm = *tempo;
        if (!AURORA_FX)
            output.clear();
        int total = output.getNumSamples();
        // Host blocks larger than advertised are safely subdivided.
        for (int start = 0; start < total; start += maxBlock) {
            int n = std::min(maxBlock, total - start);
            for (auto &b : buses)
                b.clear();
            send1.clear();
            send2.clear();
            direct.clear();
            cutoffMod.clear();
            for (int j = 0; j < 10; ++j) {
                double &p = lfoPhase[size_t(j)];
                lfoTime[size_t(j)] += n / rate;
                double frequency = val(key("lfo", j, "rate"));
                if (val(key("lfo", j, "sync")) > .5f)
                    frequency = bpm / 60 / std::pow(2., 4 - int(val(key("lfo", j, "division"))));
                p += frequency * n / rate;
                p -= std::floor(p);
                double phase = p + val(key("lfo", j, "phase"));
                phase -= std::floor(phase);
                int shape = int(val(key("lfo", j, "shape")));
                lfoValue[size_t(j)] = shape == 0   ? std::sin(2 * pi * float(phase))
                                      : shape == 1 ? 1 - 4 * std::abs(float(phase) - .5f)
                                      : shape == 2 ? 2 * float(phase) - 1
                                      : phase < .5 ? 1
                                                   : -1;
                if (shape == 4) {
                    lfoValue[size_t(j)] = 2 * lfoCurves[size_t(j)].at(float(phase)) - 1;
                }
                double elapsed = lfoTime[size_t(j)] - val(key("lfo", j, "delay"));
                float fade =
                    elapsed < 0 ? 0
                    : val(key("lfo", j, "rise")) > 0
                        ? juce::jlimit(0.f, 1.f, float(elapsed) / val(key("lfo", j, "rise")))
                        : 1;
                lfoValue[size_t(j)] *= fade;
            }
            if (AURORA_FX) {
                for (int i = 0; i < n; ++i)
                    route(0, output.getSample(0, start + i), output.getSample(1, start + i), 1, i);
            } else {
                auto &local = midiScratch;
                local.clear();
                local.addEvents(midi, start, n, -start);
                sequence(local, n);
                synth.renderNextBlock(buses[1], local, 0, n);
            }
            // Filter 1 may feed filter 2; filter 2 cannot feed filter 1 in this acyclic graph.
            for (int f = 0; f < 2; ++f) {
                float base = val(key("filter", f, "cutoff")), res = val(key("filter", f, "res"));
                int type = int(val(key("filter", f, "type")));
                for (int i = 0; i < n; ++i) {
                    float lr[2];
                    float count = std::max(1.f, cutoffMod.getSample(2, i));
                    float cutoff =
                        base * std::pow(2.f, juce::jlimit(-8.f, 8.f,
                                                          cutoffMod.getSample(f, i) / count * 8));
                    for (int c = 0; c < 2; ++c)
                        lr[c] = filters[size_t(f)][size_t(c)].tick(
                            buses[size_t(f + 2)].getSample(c, i), cutoff, res, rate, type);
                    int dest = routes[size_t(f + 5)].dest;
                    if (dest == 0 && f == 0) {
                        buses[3].addSample(0, i, lr[0]);
                        buses[3].addSample(1, i, lr[1]);
                    } else if (dest != 3)
                        route(f + 5, lr[0], lr[1], 1, i);
                }
            }
            for (int rack = 0; rack < 3; ++rack) {
                auto &b = rack == 0 ? send1 : rack == 1 ? send2 : buses[0];
                for (int j = 0; j < 4; ++j) {
                    fx[size_t(rack)][size_t(j)].process(
                        b.getWritePointer(0), b.getWritePointer(1), n,
                        int(val(key("fx", rack * 4 + j, "type"))),
                        val(key("fx", rack * 4 + j, "amount")), val(key("fx", rack * 4 + j, "mix")),
                        val(key("fx", rack * 4 + j, "time")));
                }
                if (rack < 2)
                    for (int c = 0; c < 2; ++c)
                        buses[0].addFrom(c, 0, b, c, 0, n);
            }
            float gain = val("master");
            for (int i = 0; i < n; ++i) {
                gainSmooth += (gain - gainSmooth) * .002f;
                for (int c = 0; c < 2; ++c) {
                    float x = (buses[0].getSample(c, i) + direct.getSample(c, i)) * gainSmooth;
                    output.setSample(c, start + i, AURORA_FX ? x : std::tanh(x));
                }
            }
            float peak = 0;
            for (int i = 0; i < n; ++i) {
                float left = output.getSample(0, start + i), right = output.getSample(1, start + i);
                peak = std::max(peak, std::max(std::abs(left), std::abs(right)));
                if (i % 8 == 0)
                    telemetry.push((left + right) * .5f);
            }
            telemetry.peak.store(peak);
            displayClock += n;
            if (displayClock >= rate / 60) {
                displayClock -= int(rate / 60);
                for (int slot = 0; slot < 3; ++slot) {
                    DisplayFrame frame;
                    frame.mode = int(val(key("osc", slot, "mode")));
                    frame.position = val(key("osc", slot, "position"));
                    for (int i = 0; i < 256; ++i) {
                        if (frame.mode == 0 || samples[size_t(slot)].empty())
                            frame.samples[size_t(i)] =
                                tables.read(i / 256., frame.position, 220, rate);
                        else {
                            const auto &source = samples[size_t(slot)][0].data;
                            int at = juce::jlimit(0, source.getNumSamples() - 1,
                                                  int((frame.position * .8 + i / 256.f * .2) *
                                                      (source.getNumSamples() - 1)));
                            frame.samples[size_t(i)] = source.getSample(0, at);
                        }
                    }
                    displayFrames[size_t(slot)].push(frame);
                }
            }
        }
        midi.clear();
    }
    // Asset import is serialized with the audio callback; editing is an offline operation.
    bool loadSample(int slot, const File &file, int root = 60, int low = 0, int high = 127,
                    bool append = false) {
        std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > rate * 120)
            return false;
        Zone z;
        z.root = root;
        z.low = low;
        z.high = high;
        z.data.setSize(1, int(reader->lengthInSamples));
        reader->read(&z.data, 0, z.data.getNumSamples(), 0, true, false);
        if (std::abs(reader->sampleRate - rate) > 1) {
            int len = int(z.data.getNumSamples() * rate / reader->sampleRate);
            AudioBuffer<float> res(1, len);
            for (int i = 0; i < len; ++i) {
                double at = i * reader->sampleRate / rate;
                int a = juce::jlimit(0, z.data.getNumSamples() - 1, int(at)),
                    b = std::min(a + 1, z.data.getNumSamples() - 1);
                float f = float(at - std::floor(at));
                res.setSample(0, i, z.data.getSample(0, a) * (1 - f) + z.data.getSample(0, b) * f);
            }
            z.data = std::move(res);
        }
        z.sampleRate = rate;
        const ScopedLock lock(getCallbackLock());
        synth.allNotesOff(0, false);
        if (!append)
            samples[size_t(slot)].clear();
        samples[size_t(slot)].push_back(std::move(z));
        // Analyze one 2048-sample frame into 64 harmonic amplitudes for additive resynthesis.
        std::array<float, 4096> fftdata{};
        auto &data = samples[size_t(slot)].back().data;
        for (int i = 0; i < std::min(2048, data.getNumSamples()); ++i)
            fftdata[size_t(i)] = data.getSample(0, i) * (.5f - .5f * std::cos(2 * pi * i / 2048));
        dsp::FFT fft(11);
        fft.performFrequencyOnlyForwardTransform(fftdata.data());
        float sum = 0;
        float fundamental = float(MidiMessage::getMidiNoteInHertz(root));
        for (int h = 0; h < 64; ++h) {
            int bin = juce::jlimit(1, 1023, int(fundamental * (h + 1) * 2048 / rate));
            float a = fftdata[size_t(bin)];
            spectrum[size_t(slot)][size_t(h)] = a;
            sum += a;
        }
        if (sum > 0)
            for (auto &a : spectrum[size_t(slot)])
                a /= sum;
        state.state.setProperty("sample" + String(slot), file.getFullPathName(), nullptr);
        return true;
    }
    bool loadSFZ(int slot, const File &file) {
        // Deliberately limited SFZ subset: global/group/region, sample, key, lokey,
        // hikey, pitch_keycenter. Unsupported opcodes are ignored.
        std::string text = file.loadFileAsString().toStdString();
        std::regex token(
            R"(<(global|group|region)>|([a-zA-Z_]+)=([^=<>]*?)(?=\s+[a-zA-Z_]+=|<|$))");
        std::map<std::string, std::string> global, group, region;
        std::string section;
        bool first = true;
        int loaded = 0;
        auto number = [](std::string v, int def) {
            if (v.empty())
                return def;
            try {
                return std::stoi(v);
            } catch (...) {
                String s(v);
                s = s.trim().toLowerCase();
                if (s.isEmpty())
                    return def;
                String notes = "c d ef g a b";
                int base = notes.indexOfChar(s[0]);
                if (base < 0)
                    return def;
                int offset = 1;
                if (s.length() > 1 && (s[1] == '#' || s[1] == 'b')) {
                    base += s[1] == '#' ? 1 : -1;
                    offset = 2;
                }
                return juce::jlimit(0, 127, (s.substring(offset).getIntValue() + 1) * 12 + base);
            }
        };
        auto flush = [&] {
            if (section != "region" || region["sample"].empty())
                return;
            auto path = String(region["sample"]).trim().unquoted();
            int root = number(region["pitch_keycenter"], 60), lo = number(region["lokey"], 0),
                hi = number(region["hikey"], 127);
            if (!region["key"].empty())
                root = lo = hi = number(region["key"], 60);
            if (loadSample(slot, file.getSiblingFile(path), root, lo, hi, !first)) {
                first = false;
                ++loaded;
            }
        };
        for (std::sregex_iterator it(text.begin(), text.end(), token), end; it != end; ++it) {
            auto m = *it;
            if (m[1].matched) {
                flush();
                section = m[1].str();
                if (section == "global")
                    global.clear();
                if (section == "group")
                    group = global;
                if (section == "region")
                    region = group.empty() ? global : group;
            } else {
                auto &target = section == "global" ? global : section == "group" ? group : region;
                target[m[2].str()] = m[3].str();
            }
        }
        flush();
        return loaded > 0;
    }
    bool applyIR(int rack) {
        auto &bytes = irData[size_t(rack)];
        AudioBuffer<float> buffer;
        double irRate = rate;
        if (bytes.getSize()) {
            std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(
                std::make_unique<MemoryInputStream>(bytes.getData(), bytes.getSize(), false)));
            if (!reader || reader->lengthInSamples <= 0 ||
                reader->lengthInSamples > reader->sampleRate * 10)
                return false;
            irRate = reader->sampleRate;
            buffer.setSize(2, int(reader->lengthInSamples));
            reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
        } else {
            buffer.setSize(2, 1);
            buffer.setSample(0, 0, 1);
            buffer.setSample(1, 0, 1);
        }
        for (auto &effect : fx[size_t(rack)]) {
            AudioBuffer<float> copy;
            copy.makeCopyOf(buffer);
            effect.conv.loadImpulseResponse(std::move(copy), irRate, dsp::Convolution::Stereo::yes,
                                            dsp::Convolution::Trim::yes,
                                            dsp::Convolution::Normalise::yes);
        }
        return true;
    }
    bool loadIR(int rack, const File &file) {
        MemoryBlock bytes;
        if (!file.loadFileAsData(bytes) || bytes.getSize() > 32 * 1024 * 1024)
            return false;
        const ScopedLock lock(getCallbackLock());
        irData[size_t(rack)] = std::move(bytes);
        auto &ir = irData[size_t(rack)];
        return applyIR(rack);
    }
    void getStateInformation(MemoryBlock &out) override {
        const ScopedLock lock(getCallbackLock());
        auto tree = state.copyState();
        for (int cc = 0; cc < 128; ++cc) {
            int index = midiMappings[size_t(cc)].load();
            auto *parameter =
                index >= 0 ? dynamic_cast<RangedAudioParameter *>(getParameters()[index]) : nullptr;
            tree.setProperty("cc" + String(cc), parameter ? parameter->paramID : String{}, nullptr);
        }
        for (int j = tree.getNumChildren() - 1; j >= 0; --j)
            if (tree.getChild(j).hasType("Bank"))
                tree.removeChild(j, nullptr);
        for (int s = 0; s < 3; ++s) {
            tree.setProperty(
                "irData" + String(s),
                Base64::toBase64(irData[size_t(s)].getData(), irData[size_t(s)].getSize()),
                nullptr);
            ValueTree bank("Bank");
            bank.setProperty("slot", s, nullptr);
            for (const auto &z : samples[size_t(s)]) {
                ValueTree item("Zone");
                item.setProperty("rate", z.sampleRate, nullptr);
                item.setProperty("root", z.root, nullptr);
                item.setProperty("low", z.low, nullptr);
                item.setProperty("high", z.high, nullptr);
                item.setProperty("data",
                                 Base64::toBase64(z.data.getReadPointer(0),
                                                  size_t(z.data.getNumSamples()) * sizeof(float)),
                                 nullptr);
                bank.addChild(item, -1, nullptr);
            }
            for (int h = 0; h < 64; ++h)
                bank.setProperty("h" + String(h), spectrum[size_t(s)][size_t(h)], nullptr);
            tree.addChild(bank, -1, nullptr);
        }
        if (auto xml = tree.createXml()) {
            MemoryBlock binary;
            copyXmlToBinary(*xml, binary);
            if (!encodePreset(binary, out))
                out = std::move(binary);
        }
    }
    void setStateInformation(const void *data, int size) override {
        MemoryBlock binary;
        if (!decodePreset(data, size, binary))
            return;
        auto xml = getXmlFromBinary(binary.getData(), int(binary.getSize()));
        if (!xml || !xml->hasTagName("AuroraState"))
            return;
        auto tree = ValueTree::fromXml(*xml);
        for (int cc = 0; cc < 128; ++cc) {
            var value = tree.getProperty("cc" + String(cc), -1);
            String id;
            if (value.isString())
                id = value.toString();
            else {
                int old = int(value);
                if (old >= 0 && old < int(std::size(legacyParamIds)))
                    id = legacyParamIds[old];
            }
            auto found = indices.find(id);
            midiMappings[size_t(cc)].store(found == indices.end() ? -1 : found->second);
        }
        for (auto *base : getParameters())
            if (auto *parameter = dynamic_cast<RangedAudioParameter *>(base))
                if (!tree.getChildWithProperty("id", parameter->paramID).isValid()) {
                    ValueTree child("PARAM");
                    child.setProperty("id", parameter->paramID, nullptr);
                    child.setProperty(
                        "value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
                    tree.addChild(child, -1, nullptr);
                }
        const ScopedLock lock(getCallbackLock());
        synth.allNotesOff(0, false);
        for (auto &b : samples)
            b.clear();
        for (auto bank : tree)
            if (bank.hasType("Bank")) {
                int slot = int(bank["slot"]);
                if (slot < 0 || slot > 2)
                    continue;
                for (auto item : bank) {
                    MemoryOutputStream bytes;
                    if (!Base64::convertFromBase64(bytes, item["data"].toString()))
                        continue;
                    size_t count = bytes.getDataSize() / sizeof(float);
                    if (count == 0 || count > size_t(rate * 120))
                        continue;
                    Zone z;
                    z.sampleRate = double(item.getProperty("rate", 48000));
                    z.root = int(item["root"]);
                    z.low = int(item["low"]);
                    z.high = int(item["high"]);
                    z.data.setSize(1, int(count));
                    std::memcpy(z.data.getWritePointer(0), bytes.getData(), count * sizeof(float));
                    samples[size_t(slot)].push_back(std::move(z));
                }
                for (int h = 0; h < 64; ++h)
                    spectrum[size_t(slot)][size_t(h)] = float(bank["h" + String(h)]);
            }
        for (int s = 0; s < 3; ++s) {
            MemoryOutputStream bytes;
            Base64::convertFromBase64(bytes, tree["irData" + String(s)].toString());
            irData[size_t(s)].replaceAll(bytes.getData(), bytes.getDataSize());
            auto &ir = irData[size_t(s)];
            applyIR(s);
        }
        state.replaceState(tree);
        capture(true);
    }
    const String getName() const override { return AURORA_FX ? "Aurora FX" : "Aurora"; }
    bool acceptsMidi() const override { return !AURORA_FX; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return "Init"; }
    void changeProgramName(int, const String &) override {}
    bool hasEditor() const override { return true; }
    AudioProcessorEditor *createEditor() override;
};
void Voice::startNote(int n, float vel, SynthesiserSound *, int wheel) {
    bool legato = keepEnvelope;
    note = owner.pitchFor(n);
    float from = legato ? glideNote : owner.previousNote;
    glideNote = (owner.val("portamento") > 0 &&
                 (legato || owner.synth.mode != 0 || owner.val("porta_always") > .5f))
                    ? from
                    : float(note);
    owner.previousNote = float(note);
    velocity = vel;
    pressure = 0;
    released = false;
    if (!legato) {
        phase = {};
        position = {};
        subphase = 0;
        envelopeTime = {};
        for (int s = 0; s < 3; ++s)
            for (int u = 0; u < 8; ++u)
                unisonPhase[size_t(s)][size_t(u)] =
                    std::fmod(owner.val(key("osc", s, "phase")) +
                                  owner.val(key("osc", s, "random")) * rng.nextFloat(),
                              1.);
    }
    pitchWheelMoved(wheel);
    for (int j = 0; j < 4; ++j) {
        auto &e = envs[size_t(j)];
        e.setSampleRate(getSampleRate());
        e.setParameters({owner.val(key("env", j, "a")), owner.val(key("env", j, "d")),
                         owner.val(key("env", j, "s")), owner.val(key("env", j, "r"))});
        if (!legato)
            e.noteOn();
    }
    for (int s = 0; s < 3; ++s) {
        zone[size_t(s)] = -1;
        auto &bank = owner.samples[size_t(s)];
        for (size_t j = 0; j < bank.size(); ++j)
            if (n >= bank[j].low && n <= bank[j].high) {
                zone[size_t(s)] = int(j);
                break;
            }
    }
    keepEnvelope = false;
}
void Voice::stopNote(float, bool tail) {
    if (keepEnvelope && !tail) {
        clearCurrentNote();
        return;
    }
    released = true;
    if (tail)
        for (auto &e : envs)
            e.noteOff();
    else {
        for (auto &e : envs)
            e.reset();
        clearCurrentNote();
    }
}
void Voice::renderNextBlock(AudioBuffer<float> &, int start, int n) {
    struct OscConfig {
        int mode;
        float pos, level, pan, grain;
        double tune;
        bool loop;
        int unison = 1, warp = 0;
        float detune = 0, warpAmount = .5f;
    };
    std::array<OscConfig, 3> config;
    for (int s = 0; s < 3; ++s)
        config[size_t(s)] = {int(owner.val(key("osc", s, "mode"))),
                             owner.val(key("osc", s, "position")),
                             owner.val(key("osc", s, "level")),
                             owner.val(key("osc", s, "pan")),
                             owner.val(key("osc", s, "grain")),
                             std::pow(2., owner.val(key("osc", s, "tune")) / 12.),
                             owner.val(key("osc", s, "loop")) > .5f};
    const float noiseLevel = owner.val("noise"), subLevel = owner.val("sub");
    for (int s = 0; s < 3; ++s) {
        config[size_t(s)].unison = juce::jlimit(1, 8, int(owner.val(key("osc", s, "unison"))));
        config[size_t(s)].detune = owner.val(key("osc", s, "detune"));
        config[size_t(s)].warp = int(owner.val(key("osc", s, "warp")));
        config[size_t(s)].warpAmount = owner.val(key("osc", s, "warp_amount"));
    }
    float glideMs = owner.val("portamento");
    if (owner.val("porta_always") < .5f)
        glideMs *= std::max(.1f, std::abs(note - glideNote) / 12.f);
    float glideRate =
        glideMs > 0 ? float(1 - std::exp(-1 / (getSampleRate() * glideMs * .0002))) : 1;
    for (int i = start; i < start + n; ++i) {
        std::array<float, 4> en{};
        for (int j = 0; j < 4; ++j) {
            en[size_t(j)] = envs[size_t(j)].getNextSample();
            envelopeTime[size_t(j)] += 1 / getSampleRate();
            if (owner.val(key("env", j, "mode")) > .5f) {
                float position =
                    float(envelopeTime[size_t(j)]) / owner.val(key("env", j, "duration"));
                if (owner.val(key("env", j, "loop")) > .5f)
                    position -= std::floor(position);
                en[size_t(j)] *= owner.envCurves[size_t(j)].at(position);
            }
        }
        if (!envs[0].isActive()) {
            clearCurrentNote();
            break;
        }
        std::array<float, 11> mod{};
        for (const auto &m : owner.mods) {
            int src = m.source, dest = m.target;
            if (!src || !dest || m.depth == 0)
                continue;
            float value = src == 1    ? velocity
                          : src == 2  ? note / 127.f
                          : src == 3  ? pressure
                          : src < 8   ? en[size_t(src - 4)]
                          : src < 18  ? owner.lfoValue[size_t(src - 8)]
                          : src == 26 ? owner.modwheel.load()
                                      : owner.macros[size_t(src - 18)];
            if (m.curve != 1)
                value = std::copysign(std::pow(std::abs(value), m.curve), value);
            mod[size_t(dest)] += value * m.depth;
        }
        owner.cutoffMod.addSample(0, i, mod[9]);
        owner.cutoffMod.addSample(1, i, mod[10]);
        owner.cutoffMod.addSample(2, i, 1);
        glideNote += (note - glideNote) * glideRate;
        double hz = 440 * std::pow(2., (glideNote - 69) / 12. + mod[7] * 2.) * bend;
        for (int s = 0; s < 3; ++s) {
            const auto &cfg = config[size_t(s)];
            if (cfg.level == 0 && mod[size_t(s + 1)] <= 0)
                continue;
            int mode = cfg.mode;
            double frequency = hz * cfg.tune;
            double &p = phase[size_t(s)];
            float pos = juce::jlimit(0.f, 1.f, cfg.pos + mod[size_t(s + 4)]);
            float x = 0;
            if (mode == 0) {
                for (int u = 0; u < cfg.unison; ++u) {
                    double &up = unisonPhase[size_t(s)][size_t(u)];
                    double detune =
                        cfg.unison == 1 ? 0 : (2. * u / (cfg.unison - 1) - 1) * cfg.detune;
                    double uf = frequency * std::pow(2., detune / 1200.), phase = up;
                    if (cfg.warp == 1)
                        phase = std::pow(phase, .25 * std::pow(16., cfg.warpAmount));
                    if (cfg.warp == 2) {
                        phase *= 1 + cfg.warpAmount * 7;
                        phase -= std::floor(phase);
                    }
                    float value = owner.tables.read(
                        phase, pos, uf * (cfg.warp == 2 ? 1 + cfg.warpAmount * 7 : 1),
                        getSampleRate());
                    if (cfg.warp == 3)
                        value = std::sin(value * (1 + cfg.warpAmount * 5));
                    x += value / cfg.unison;
                    up += uf / getSampleRate();
                    up -= std::floor(up);
                }
            } else if (mode == 4) {
                for (int h = 1; h <= 64 && frequency * h < getSampleRate() * .45; ++h)
                    x += owner.spectrum[size_t(s)][size_t(h - 1)] *
                         std::sin(2 * pi * float(p) * h + pos * pi * h);
            } else if (zone[size_t(s)] >= 0) {
                const auto &z = owner.samples[size_t(s)][size_t(zone[size_t(s)])];
                int len = z.data.getNumSamples();
                double &cursor = position[size_t(s)];
                double ratio = frequency / MidiMessage::getMidiNoteInHertz(z.root) * z.sampleRate /
                               getSampleRate();
                auto read = [&](double at) {
                    if (at < 0 || at >= len - 1)
                        return 0.f;
                    int k = int(at);
                    float f = float(at - k);
                    return z.data.getSample(0, k) * (1 - f) + z.data.getSample(0, k + 1) * f;
                };
                if (mode == 3) {
                    double grain = cfg.grain * getSampleRate() * .001;
                    double a = std::fmod(cursor, grain), b = std::fmod(cursor + grain * .5, grain);
                    x = read(pos * std::max(0., len - grain * ratio) + a * ratio) *
                            (.5f - .5f * std::cos(2 * pi * float(a / grain))) +
                        read(pos * std::max(0., len - grain * ratio) + b * ratio) *
                            (.5f - .5f * std::cos(2 * pi * float(b / grain)));
                    cursor += 1;
                } else {
                    x = read(cursor);
                    cursor += ratio;
                    if (cfg.loop && cursor >= len - 1)
                        cursor = std::fmod(cursor, double(std::max(1, len - 1)));
                }
            }
            p += frequency / getSampleRate();
            p -= std::floor(p);
            float level = juce::jlimit(0.f, 1.f, cfg.level + mod[size_t(s + 1)]) * velocity * .25f;
            float pan = juce::jlimit(-1.f, 1.f, cfg.pan + mod[8]);
            owner.route(s, x * level * std::sqrt((1 - pan) * .5f),
                        x * level * std::sqrt((1 + pan) * .5f), en[0], i);
        }
        float noise = (rng.nextFloat() * 2 - 1) * noiseLevel * velocity * .12f;
        owner.route(3, noise, noise, en[0], i);
        float sub = std::sin(2 * pi * float(subphase)) * subLevel * velocity * .25f;
        subphase += hz * .5 / getSampleRate();
        subphase -= std::floor(subphase);
        owner.route(4, sub, sub, en[0], i);
    }
}
#include "Editor.h"
AudioProcessorEditor *Aurora::createEditor() { return new Editor(*this); }
AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new Aurora; }
bool auroraImportSampleForTest(AudioProcessor &p, int slot, const File &f) {
    return dynamic_cast<Aurora &>(p).loadSample(slot, f);
}
bool auroraImportSFZForTest(AudioProcessor &p, int slot, const File &f) {
    return dynamic_cast<Aurora &>(p).loadSFZ(slot, f);
}
bool auroraUIForTest(AudioProcessor &processor, const File &folder) {
    auto &p = dynamic_cast<Aurora &>(processor);
    Editor editor(p);
    bool okay = true;
    for (int page = 0; page < 6; ++page) {
        editor.showPage(page);
        okay &= editor.pageFitsForTest();
        auto image = editor.createComponentSnapshot(editor.getLocalBounds());
        auto file = folder.getChildFile("interface-" + String(page) + ".png");
        file.deleteFile();
        auto stream = file.createOutputStream();
        PNGImageFormat png;
        okay &= stream && png.writeImageToStream(image, *stream);
    }
    editor.setSize(660, 390);
    editor.showPage(0);
    okay &= editor.pageFitsForTest();
    auto image = editor.createComponentSnapshot(editor.getLocalBounds());
    auto file = folder.getChildFile("interface-small.png");
    file.deleteFile();
    auto stream = file.createOutputStream();
    PNGImageFormat png;
    okay &= stream && png.writeImageToStream(image, *stream);
    aurora_ui::Knob knob(p, "osc0_level", "Level", 1);
    DragAndDropTarget::SourceDetails details(var(8), &editor, {});
    knob.itemDropped(details);
    okay &= aurora_ui::read(p, "mod0_source") == 8 && aurora_ui::read(p, "mod0_target") == 1;
    aurora_ui::write(p, "fx2s0_type", 1);
    aurora_ui::write(p, "fx2s1_type", 5);
    aurora_ui::Card card(p, "Slot 2");
    card.enableFXDrag(2, 1);
    DragAndDropTarget::SourceDetails fxDetails(var("fx:2:0"), &editor, {});
    card.itemDropped(fxDetails);
    okay &= aurora_ui::read(p, "fx2s0_type") == 5 && aurora_ui::read(p, "fx2s1_type") == 1;
    p.midiLearn.store(p.indices["master"]);
    MidiBuffer events;
    events.addEvent(MidiMessage::controllerEvent(1, 74, 64), 0);
    AudioBuffer<float> block(2, 256);
    processor.processBlock(block, events);
    okay &= p.midiMappings[74].load() == p.indices["master"] &&
            std::abs(aurora_ui::read(p, "master") - 64 / 127.f) < .001f;
    p.state.copyState();
    p.undo.clearUndoHistory();
    float before = aurora_ui::read(p, "master");
    p.undo.beginNewTransaction("Test edit");
    aurora_ui::write(p, "master", .25f);
    p.state.copyState();
    okay &= p.undo.undo() && std::abs(aurora_ui::read(p, "master") - before) < .001f;
    okay &= p.undo.redo() && std::abs(aurora_ui::read(p, "master") - .25f) < .001f;
    editor.setSize(960, 567);
    editor.showPage(0);
    editor.refreshForTest();
    auto modulationPreview = folder.getChildFile("interface-modulation.png");
    modulationPreview.deleteFile();
    auto modulationStream = modulationPreview.createOutputStream();
    okay &= modulationStream &&
            png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()),
                                   *modulationStream);
    return okay;
}
int auroraGpuForTest(AudioProcessor &processor) {
    Editor editor(dynamic_cast<Aurora &>(processor));
    return editor.testGPU();
}
bool auroraPerformanceForTest() {
    Aurora processor;
    processor.prepareToPlay(48000, 256);
    auto set = [&](String id, float value) {
        auto *parameter = processor.state.getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    AudioBuffer<float> audio(2, 256);
    MidiBuffer midi;
    set("voice_mode", 2);
    set("portamento", 100);
    processor.capture(true);
    midi.addEvent(MidiMessage::noteOn(1, 60, .8f), 0);
    midi.addEvent(MidiMessage::noteOn(1, 72, .8f), 128);
    processor.processBlock(audio, midi);
    int active = 0;
    for (int i = 0; i < 16; ++i)
        active += processor.synth.getVoice(i)->isVoiceActive();
    bool okay = active == 1 && audio.getMagnitude(0, 256) > 0;
    midi.addEvent(MidiMessage::noteOff(1, 72), 0);
    processor.processBlock(audio, midi);
    okay &= processor.synth.getVoice(0)->getCurrentlyPlayingNote() == 60;
    set("env0_mode", 1);
    set("env0_tension", .7f);
    set("osc0_unison", 4);
    set("osc0_detune", 25);
    set("osc0_warp", 1);
    processor.capture(true);
    for (int i = 0; i < 40; ++i) {
        processor.processBlock(audio, midi);
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < 256; ++j)
                okay &= std::isfinite(audio.getSample(c, j));
    }
    CurveShape curve;
    curve.count = 2;
    curve.x[0] = 0;
    curve.x[1] = 1;
    curve.y[0] = 0;
    curve.y[1] = 1;
    okay &= std::abs(curve.at(.5f) - .5f) < 1e-5f;
    curve.tension = .8f;
    okay &= curve.at(.5f) > .5f;
    return okay;
}

bool auroraRoutingModulationForTest() {
    auto render = [](int scenario) {
        Aurora p;
        auto set = [&](String id, float value) {
            auto *param = p.state.getParameter(id);
            param->setValueNotifyingHost(param->convertTo0to1(value));
        };
        set("noise", 0);
        set("sub", 0);
        set("osc0_random", 0);
        set("osc0_level", .8f);
        set("route0_dest", scenario == 0 ? .25f : 0.f);
        set("route0_balance", -100);
        set("filter0_cutoff", scenario <= 1 ? 150.f : 8000.f);
        std::unique_ptr<aurora_ui::Knob> control;
        if (scenario == 3) {
            set("env1_a", .001f);
            set("env1_s", 1);
            control = std::make_unique<aurora_ui::Knob>(p, "filter0_cutoff", "Cutoff", 9);
            control->itemDropped(DragAndDropTarget::SourceDetails(var(5), nullptr, {}));
        }
        if (scenario == 4) {
            set("lfo0_rate", 5);
            set("lfo0_phase", .25f);
            control = std::make_unique<aurora_ui::Knob>(p, "osc0_level", "Level", 1);
            control->itemDropped(DragAndDropTarget::SourceDetails(var(8), nullptr, {}));
        }
        p.prepareToPlay(48000, 256);
        AudioBuffer<float> result(2, 8192), block(2, 256);
        MidiBuffer midi;
        for (int offset = 0; offset < 8192; offset += 256) {
            midi.clear();
            if (!offset)
                midi.addEvent(MidiMessage::noteOn(1, 72, .8f), 0);
            p.processBlock(block, midi);
            for (int c = 0; c < 2; ++c)
                result.copyFrom(c, offset, block, c, 0, 256);
        }
        p.releaseResources();
        return result;
    };
    auto dry = render(0), filtered = render(1), base = render(2), env = render(3), lfo = render(4);
    auto difference = [](const AudioBuffer<float> &a, const AudioBuffer<float> &b) {
        double energy = 0;
        for (int i = 0; i < a.getNumSamples(); ++i) {
            double d = a.getSample(0, i) - b.getSample(0, i);
            energy += d * d;
        }
        return energy;
    };
    return filtered.getRMSLevel(0, 1024, 4096) < dry.getRMSLevel(0, 1024, 4096) * .5f &&
           difference(base, env) > .1 && difference(base, lfo) > .1;
}
