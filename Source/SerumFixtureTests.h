#pragma once
#include "ImportedWavetable.h"
#include "SerumImport.h"

extern bool auroraSerumForTest(const juce::File &, juce::String &, bool);
namespace aurora {
inline void fixtureCbor(juce::OutputStream &output, const juce::var &value) {
    auto head = [&](int major, uint64_t n) {
        if (n < 24)
            output.writeByte(char((major << 5) | int(n)));
        else if (n < 256) {
            output.writeByte(char((major << 5) | 24));
            output.writeByte(char(n));
        } else {
            output.writeByte(char((major << 5) | 26));
            output.writeIntBigEndian(int(n));
        }
    };
    if (auto *object = value.getDynamicObject()) {
        const auto &props = object->getProperties();
        head(5, uint64_t(props.size()));
        for (int i = 0; i < props.size(); ++i) {
            fixtureCbor(output, props.getName(i).toString());
            fixtureCbor(output, props.getValueAt(i));
        }
    } else if (value.isArray()) {
        head(4, uint64_t(value.size()));
        for (const auto &item : *value.getArray())
            fixtureCbor(output, item);
    } else if (value.isString()) {
        auto text = value.toString();
        head(3, uint64_t(text.getNumBytesAsUTF8()));
        output.write(text.toRawUTF8(), size_t(text.getNumBytesAsUTF8()));
    } else if (value.isBool())
        output.writeByte(char(bool(value) ? 0xf5 : 0xf4));
    else if (value.isInt() || value.isInt64()) {
        auto n = juce::int64(value);
        head(n < 0 ? 1 : 0, uint64_t(n < 0 ? -1 - n : n));
    } else if (value.isDouble()) {
        output.writeByte(char(0xfb));
        output.writeDoubleBigEndian(double(value));
    } else
        output.writeByte(char(0xf6));
}
inline juce::MemoryBlock fixtureCarrier(const juce::var &payload, int kind=2) {
    juce::MemoryOutputStream cbor;
    if(kind==0)cbor.writeText(juce::JSON::toString(payload,true),false,false,nullptr);
    else fixtureCbor(cbor, payload);
    juce::MemoryBlock packed(ZSTD_compressBound(cbor.getDataSize()));
    auto length =
        ZSTD_compress(packed.getData(), packed.getSize(), cbor.getData(), cbor.getDataSize(), 1);
    if(kind!=2){juce::MemoryOutputStream zipped;{juce::GZIPCompressorOutputStream compressor(zipped,6,15);compressor.write(cbor.getData(),cbor.getDataSize());}packed=zipped.getMemoryBlock();length=packed.getSize();}
    juce::MemoryOutputStream result;
    result.write("XferJson\0", 9);
    juce::String metadata =
        "{\"fileType\":\"SerumPreset\",\"presetName\":\"Aurora synthetic fixture\"}";
    result.writeInt64(metadata.getNumBytesAsUTF8());
    result.write(metadata.toRawUTF8(), size_t(metadata.getNumBytesAsUTF8()));
    result.writeInt(int(cbor.getDataSize()));
    result.writeInt(kind);
    result.write(packed.getData(), length);
    return result.getMemoryBlock();
}
inline void serumFixtureTests(const std::function<void(bool, const char *)> &check) {
    using namespace juce;
    auto payload = JSON::parse(R"JSON({
      "product":"Serum2","Oscillator0":{
        "plainParams":{"kParamEnable":1,"kParamVolume":0.7,"kParamUnison":2},
        "WTOsc0":{"relativePathToWT":"AuroraFixture.wav","plainParams":{"kParamTablePos":128}}
      },
      "Env0":{"plainParams":{"kParamAttack":0.01,"kParamDecay":0.1,"kParamSustain":0.7,"kParamRelease":0.1}},
      "LFO0":{"plainParams":{"kParamBeatSync":0,"kParamRate":1},"curveData":{"xVals":[0,0.5,1],"yVals":[1,0,1]}},
      "ModSlot0":{"source":[6,0],"destModuleTypeString":"WTOsc","destModuleID":0,"destModuleParamName":"kParamTablePos","plainParams":{"kParamAmount":50}}
    })JSON");
    bool carriers=true;
    for(int kind=0;kind<3;++kind){auto carrier=fixtureCarrier(payload,kind);SerumDocument doc;SerumImportReport r;
        carriers &= decodeSerum(carrier.getData(),carrier.getSize(),doc,r) && doc.encoding==kind && doc.payload.hasProperty("Oscillator0");
        auto corrupt=carrier;static_cast<uint8_t*>(corrupt.getData())[corrupt.getSize()-1]^=0x40;r={};
        carriers &= !decodeSerum(corrupt.getData(),corrupt.getSize(),doc,r);
    }
    check(carriers,"Preset carriers JSON/zlib, CBOR/zlib and CBOR/Zstandard decode and reject corruption");
    {
        SerumDocument d;d.payload=JSON::parse(R"JSON({"Oscillator0":{"plainParams":{"kParamEnable":1,"kParamPan":0.75,"kParamUnison":16,"kParamDetune":0.6},"WTOsc0":{"plainParams":{"kParamInitialPhase":90,"kParamRandomPhase":25,"kParamWarpMenu":"kSync","kParamWarp":0.5}}}})JSON");
        SerumImportReport r;auto mapped=mapSerum(d,r);
        check(mapped.values["osc0_pan"]==.5f && mapped.values["osc0_phase"]==.25f && mapped.values["osc0_random"]==.25f && mapped.values["osc0_wt_amount1"]==.5f && mapped.values["osc0_wt_unison"]==16 && std::abs(mapped.values["osc0_wt_detune"]-.6f)<1e-6f,"Preset per-parameter units: pan, degrees, random percent, warp amount and 16-way unison");
        d.payload=JSON::parse(R"JSON({"Oscillator0":{"plainParams":"default","WTOsc0":{"plainParams":"default"}}})JSON");r={};mapped=mapSerum(d,r);
        check(mapped.values["osc0_pan"]==0 && mapped.values["osc0_phase"]==.5f && mapped.values["osc0_random"]==1 && mapped.values["osc0_wt_detune"]==.5f,"Preset default marker uses declared defaults rather than zero");
    }
    auto bytes = fixtureCarrier(payload);
    SerumDocument document;
    SerumImportReport report;
    bool decoded = decodeSerum(bytes.getData(), bytes.getSize(), document, report);
    auto plan = mapSerum(document, report);
    check(decoded && plan.values["mod0_source"] == 8 && plan.values["mod0_target"] == 4 &&
              std::abs(plan.values["env0_a"] - .01f) < 1e-6f && plan.values["lfo0_point0"] == 0,
          "Serum XferJson/Zstd/CBOR maps oscillator, envelope, LFO polarity and modulation");
    SerumImportReport invalid;
    SerumDocument unused;
    bool truncated = !decodeSerum(bytes.getData(), bytes.getSize() - 7, unused, invalid);
    auto damaged = bytes;
    std::memset(static_cast<char *>(damaged.getData()) + 9, 0xff, 8);
    invalid = {};
    bool badLength = !decodeSerum(damaged.getData(), damaged.getSize(), unused, invalid);
    auto unsupported = bytes;
    MemoryInputStream header(bytes, false);
    header.skipNextBytes(9);
    auto metadataSize = header.readInt64();
    std::memset(static_cast<char *>(unsupported.getData()) + 17 + size_t(metadataSize) + 4, 0, 4);
    invalid = {};
    check(truncated && badLength &&
              !decodeSerum(unsupported.getData(), unsupported.getSize(), unused, invalid),
          "Serum truncated payload, oversized metadata and unsupported encoding rejected");
    const uint8_t incomplete[]{0xa1, 0x61, 'x'};
    MemoryBlock malformed(incomplete, sizeof(incomplete));
    var v;
    std::vector<uint8_t> nested(80, 0x81);
    nested.push_back(0);
    MemoryBlock deep(nested.data(), nested.size());
    check(!SerumCbor(malformed).read(v) && !SerumCbor(deep).read(v),
          "Serum CBOR truncation and nesting limits");
    auto folder = File::getCurrentWorkingDirectory().getChildFile("work/serum-fixture");
    folder.getChildFile("Tables").createDirectory();
    auto wave = folder.getChildFile("Tables/AuroraFixture.wav");
    wave.deleteFile();
    AudioBuffer<float> table(1, 4096);
    for (int i = 0; i < 4096; ++i)
        table.setSample(0, i, std::sin(2 * pi * (i % 2048) / 2048.f) * (i < 2048 ? 1.f : .5f));
    {
        WavAudioFormat format;
        auto stream = wave.createOutputStream();
        std::unique_ptr<AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 44100, 1, 24, {}, 0));
        if (writer)
            writer->writeFromAudioSampleBuffer(table, 0, 4096);
    }
    auto preset = folder.getChildFile("Fixture.SerumPreset");
    preset.replaceWithData(bytes.getData(), bytes.getSize());
    String details;
    check(auroraSerumForTest(preset, details, true),
          "Serum external wavetable / audio / native state round-trip / invalid import preserves "
          "sound");
    if (details.startsWith("ERROR"))
        std::cout << details << "\n";
    folder.getChildFile("Samples").createDirectory();
    auto sample = folder.getChildFile("Samples/AuroraSample.wav");
    sample.deleteFile();
    {
        WavAudioFormat format;
        auto stream = sample.createOutputStream();
        std::unique_ptr<AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 44100, 1, 24, {}, 0));
        if (writer)
            writer->writeFromAudioSampleBuffer(table, 0, 4096);
    }
    auto osc = payload["Oscillator0"];
    osc["plainParams"].getDynamicObject()->setProperty("kParamType", "kOsc_Sample");
    osc.getDynamicObject()->setProperty(
        "SampleOsc0", JSON::parse("{\"samplePathRelative\":\"AuroraSample.wav\"}"));
    auto sampleBytes = fixtureCarrier(payload);
    auto samplePreset = folder.getChildFile("SampleFixture.serumpreset");
    samplePreset.replaceWithData(sampleBytes.getData(), sampleBytes.getSize());
    check(auroraSerumForTest(samplePreset, details, false),
          "Serum relative sample reference / audio / native state round-trip");
    if (details.startsWith("ERROR"))
        std::cout << details << "\n";
    std::vector<float> sine(2048);
    for (int i = 0; i < 2048; ++i)
        sine[size_t(i)] = std::sin(2 * pi * i / 2048.f);
    ImportedWavetable imported;
    check(imported.build(std::move(sine)) &&
              std::abs(imported.levelData(0,3)[64] - 1) < .00002f &&
              std::abs(imported.read(.25, 0, 220, 48000) - 1.00317417f) < .0003f,
          "Imported wavetable FFT retains fundamental amplitude before recovered reader gain");
}
} // namespace aurora
