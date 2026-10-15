#include "Dsp.h"
#include <iostream>
extern juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter();
extern bool auroraImportSampleForTest(juce::AudioProcessor &, int, const juce::File &);
extern bool auroraImportSFZForTest(juce::AudioProcessor &, int, const juce::File &);
extern bool auroraUIForTest(juce::AudioProcessor &, const juce::File &);
extern int auroraGpuForTest(juce::AudioProcessor &);
extern bool auroraPerformanceForTest();
extern bool auroraRoutingModulationForTest();
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    using namespace aurora;
    int fail = 0;
    auto check = [&](bool ok, const char *message) {
        std::cout << (ok ? "PASS " : "FAIL ") << message << "\n";
        if (!ok)
            ++fail;
    };
    check(std::abs(nonlinear(1) - .85185185f) < 1e-6f, "nonlinear polynomial");
    Delay d;
    d.prepare(48000);
    d.push(1);
    check(std::abs(d.read(1) - 1) < 1e-6f, "delay impulse / wrap indexing");
    d.push(0);
    check(std::abs(d.read(1.5f) - .5f) < 1e-6f, "fractional delay interpolation");
    SVF f;
    double energy = 0;
    for (int i = 0; i < 100000; ++i) {
        float v = f.tick(i == 0 ? 1.f : 0.f, 18000, .99f, 48000);
        if (!std::isfinite(v)) {
            fail++;
            break;
        }
        energy += v * v;
    }
    check(energy > 0 && energy < 100, "SVF bounded impulse response");
    for (int type = 1; type <= 17; ++type) {
        Effect e;
        e.prepare(48000, 256);
        float l[256], r[256];
        bool finite = true;
        for (int block = 0; block < 100; ++block) {
            for (int i = 0; i < 256; ++i) {
                l[i] = .1f * std::sin(2 * pi * 440 * (block * 256 + i) / 48000.f);
                r[i] = l[i];
            }
            e.process(l, r, 256, type, .75f, .8f, .4f);
            for (int i = 0; i < 256; ++i)
                finite &= std::isfinite(l[i]) && std::isfinite(r[i]) && std::abs(l[i]) < 20 &&
                          std::abs(r[i]) < 20;
        }
        check(finite, ("effect " + std::to_string(type) + " stability").c_str());
    }
    {
        Effect e;
        e.prepare(48000, 256);
        double upSin = 0, upCos = 0, downSin = 0, downCos = 0;
        float l[256], r[256];
        for (int block = 0; block < 200; ++block) {
            for (int i = 0; i < 256; ++i)
                l[i] = r[i] = .1f * std::sin(2 * pi * 440 * (block * 256 + i) / 48000.f);
            e.process(l, r, 256, 11, .5f, 1, .55f);
            if (block > 20)
                for (int i = 0; i < 256; ++i) {
                    double t = (block * 256 + i) / 48000.;
                    upSin += l[i] * std::sin(2 * pi * 540 * t);
                    upCos += l[i] * std::cos(2 * pi * 540 * t);
                    downSin += l[i] * std::sin(2 * pi * 340 * t);
                    downCos += l[i] * std::cos(2 * pi * 340 * t);
                }
        }
        check(upSin * upSin + upCos * upCos > 10 * (downSin * downSin + downCos * downCos),
              "Bode +100 Hz shift / unwanted sideband rejection");
    }
    std::unique_ptr<juce::AudioProcessor> plugin(createPluginFilter());
    plugin->prepareToPlay(48000, 256);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    double total = 0;
    bool finite = true;
    for (int b = 0; b < 200; ++b) {
        midi.clear();
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, .8f), 32);
        if (b == 100)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 64);
        plugin->processBlock(audio, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i) {
                float x = audio.getSample(c, i);
                finite &= std::isfinite(x) && std::abs(x) <= 1;
                total += x * x;
            }
    }
    check(finite && total > 1, "synth MIDI note-on/off produces bounded audio");
    check(audio.getMagnitude(0, 256) < .0001f, "release decays to silence");
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    auto copy = std::unique_ptr<juce::AudioProcessor>(createPluginFilter());
    copy->setStateInformation(state.getData(), int(state.getSize()));
    juce::MemoryBlock restored;
    copy->getStateInformation(restored);
    check(state == restored, "preset state round-trip");
    auto legacyFile =
        juce::File(__FILE__).getParentDirectory().getChildFile("Fixtures/Legacy.aurora");
    juce::MemoryBlock legacyState;
    bool legacyOk = legacyFile.loadFileAsData(legacyState);
    auto legacy = std::unique_ptr<juce::AudioProcessor>(createPluginFilter());
    if (legacyOk) {
        legacy->setStateInformation(legacyState.getData(), int(legacyState.getSize()));
        bool foundUnison = false;
        for (auto *param : legacy->getParameters())
            if (auto *id = dynamic_cast<juce::AudioProcessorParameterWithID *>(param))
                if (id->paramID == "osc0_unison") {
                    foundUnison = true;
                    legacyOk &= std::abs(param->getValue() - param->getDefaultValue()) < 1e-6f;
                }
        legacyOk &= foundUnison;
        legacy->prepareToPlay(48000, 256);
        juce::AudioBuffer<float> legacyAudio(2, 256);
        juce::MidiBuffer legacyMidi;
        legacyMidi.addEvent(juce::MidiMessage::noteOn(1, 60, .7f), 0);
        legacy->processBlock(legacyAudio, legacyMidi);
        legacyOk &= legacyAudio.getMagnitude(0, 256) > .0001f;
        legacy->releaseResources();
    }
    check(legacyOk, "previous-version preset loads with new parameter defaults and audio");
    audio.setSize(2, 1024);
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, .6f), 700);
    plugin->processBlock(audio, midi);
    check(audio.getMagnitude(0, 700) < .0001f && audio.getMagnitude(700, 324) > .001f,
          "oversized host block / MIDI sample offset");
    plugin->releaseResources();
    auto set = [&](const juce::String &id, float value) {
        for (auto *param : plugin->getParameters())
            if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(param))
                if (p->paramID == id)
                    p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    juce::File outputs = juce::File::getCurrentWorkingDirectory().getChildFile("outputs/Aurora");
    outputs.createDirectory();
    juce::File asset =
        juce::File::getCurrentWorkingDirectory().getChildFile("work/test-sample.wav");
    asset.deleteFile();
    juce::WavAudioFormat wav;
    juce::AudioBuffer<float> tone(1, 48000);
    for (int i = 0; i < 48000; ++i)
        tone.setSample(0, i, .4f * std::sin(2 * pi * 261.6256f * i / 48000));
    {
        auto stream = asset.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.release(), 48000, 1, 16, {}, 0));
        if (writer)
            writer->writeFromAudioSampleBuffer(tone, 0, 48000);
    }
    plugin->prepareToPlay(48000, 256);
    check(auroraImportSampleForTest(*plugin, 0, asset), "sample import");
    set("sub", 0);
    for (int mode = 1; mode <= 4; ++mode) {
        set("osc0_mode", float(mode));
        audio.setSize(2, 256);
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, .8f), 0);
        plugin->processBlock(audio, midi);
        check(audio.getMagnitude(0, 256) > .00001f,
              ("engine " + std::to_string(mode) + " generates sample-derived audio").c_str());
        plugin->releaseResources();
    }
    juce::File sfz = asset.withFileExtension("sfz");
    sfz.replaceWithText(
        "<group> pitch_keycenter=60\n<region> sample=test-sample.wav lokey=0 hikey=63\n<region> "
        "sample=test-sample.wav lokey=64 hikey=127 pitch_keycenter=72\n");
    check(auroraImportSFZForTest(*plugin, 0, sfz), "SFZ two-zone import");
    plugin->getStateInformation(state);
    copy->setStateInformation(state.getData(), int(state.getSize()));
    copy->getStateInformation(restored);
    check(state == restored, "embedded samples / zones preset round-trip");
    set("osc0_mode", 0);
    set("osc0_position", .45f);
    set("osc1_level", .3f);
    set("osc1_tune", 12);
    set("fx2s0_type", 10);
    set("fx2s1_type", 7);
    set("fx2s1_mix", .22f);
    set("env0_r", 1.2f);
    set("sub", .12f);
    plugin->prepareToPlay(48000, 256);
    juce::AudioBuffer<float> demo(2, 48000 * 4);
    for (int off = 0; off < demo.getNumSamples(); off += 256) {
        int size = std::min(256, demo.getNumSamples() - off);
        audio.setSize(2, size);
        midi.clear();
        if (off == 0)
            for (int note : {60, 64, 67})
                midi.addEvent(juce::MidiMessage::noteOn(1, note, .7f), 0);
        if (off == 96000)
            for (int note : {60, 64, 67})
                midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        plugin->processBlock(audio, midi);
        for (int c = 0; c < 2; ++c)
            demo.copyFrom(c, off, audio, c, 0, size);
    }
    auto demoFile = outputs.getChildFile("demo.wav");
    demoFile.deleteFile();
    {
        auto stream = demoFile.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.release(), 48000, 2, 24, {}, 0));
        if (writer)
            writer->writeFromAudioSampleBuffer(demo, 0, demo.getNumSamples());
    }
    plugin->getStateInformation(state);
    outputs.getChildFile("Demo.aurora").replaceWithData(state.getData(), state.getSize());
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
        if (editor) {
            auto image = editor->createComponentSnapshot(editor->getLocalBounds());
            auto preview = outputs.getChildFile("interface.png");
            preview.deleteFile();
            auto stream = preview.createOutputStream();
            juce::PNGImageFormat png;
            if (stream)
                png.writeImageToStream(image, *stream);
            check(image.isValid(), "editor software-rendered preview");
        }
    }
    check(auroraUIForTest(*plugin, outputs),
          "UI pages / resize / modulation drop / FX reorder / MIDI learn / undo-redo");
    check(auroraPerformanceForTest(),
          "Mono/legato fallback / portamento / unison / Bezier envelope stability");
    check(auroraRoutingModulationForTest(),
          "OSC filter routing / envelope cutoff and LFO level affect rendered audio");
    int gpu = auroraGpuForTest(*plugin);
    if (gpu == 0)
        std::cout << "SKIP OpenGL native driver not available\n";
    else
        check(gpu > 0, "OpenGL shader compilation on native context");
    if (argc > 1) {
        juce::AudioPluginFormatManager manager;
        manager.addFormat(new juce::VST3PluginFormat);
        for (int a = 1; a < argc; ++a) {
            juce::OwnedArray<juce::PluginDescription> descriptions;
            manager.getFormat(0)->findAllTypesForFile(
                descriptions, juce::File(juce::String(argv[a])).getFullPathName());
            check(descriptions.size() > 0, "VST3 factory enumerates component");
            if (descriptions.isEmpty())
                continue;
            juce::String error;
            auto instance = manager.createPluginInstance(*descriptions[0], 48000, 256, error);
            check(instance != nullptr, "VST3 host creates component/controller");
            if (!instance) {
                std::cout << error << "\n";
                continue;
            }
            instance->prepareToPlay(48000, 256);
            juce::AudioBuffer<float> block(2, 256);
            juce::MidiBuffer events;
            block.clear();
            if (instance->acceptsMidi())
                events.addEvent(juce::MidiMessage::noteOn(1, 60, .8f), 0);
            else
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 256; ++i)
                        block.setSample(c, i, .1f * std::sin(2 * pi * 440 * i / 48000));
            instance->processBlock(block, events);
            check(block.getMagnitude(0, 256) > 0 && std::isfinite(block.getMagnitude(0, 256)),
                  "VST3 renders audio in host");
            std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorIfNeeded());
            check(editor != nullptr, "VST3 editor constructs");
            editor.reset();
            instance->releaseResources();
        }
    }
    return fail ? 1 : 0;
}
