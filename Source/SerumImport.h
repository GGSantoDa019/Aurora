#pragma once
#include "Preset.h"
#include "Cbor.h"
#include <limits>
#include <map>
#include "WavetableAlgorithms.h"

namespace aurora {
// Independent reader of the documented XferJson carrier and RFC 8949 CBOR.
// References: docs/SERUM_IMPORT.md. No Serum executable code is used.
struct SerumImportReport {
    juce::String error, name;
    juce::StringArray notes;
    int parameters = 0, modulations = 0, assets = 0;
    void note(const juce::String &text) { notes.addIfNotAlreadyThere(text); }
    juce::String text() const {
        juce::String result = error.isNotEmpty()
                                  ? "Import failed: " + error
                                  : "Approximate import: " + name + "\n" +
                                        juce::String(parameters) + " parameters, " +
                                        juce::String(modulations) + " modulation routes, " +
                                        juce::String(assets) + " audio assets.\n";
        for (const auto &note : notes)
            result += "\n- " + note;
        return result;
    }
};


struct SerumDocument {
    juce::var metadata, payload;
    int encoding=2;
};
inline bool decodeSerum(const void *data, size_t size, SerumDocument &doc,
                        SerumImportReport &report) {
    constexpr size_t limit = 64 * 1024 * 1024;
    if (size < 25 || size > limit || std::memcmp(data, "XferJson\0", 9)) {
        report.error = "Not a supported .SerumPreset file (XferJson header).";
        return false;
    }
    juce::MemoryInputStream input(data, size, false);
    input.skipNextBytes(9);
    auto metadataBytes = uint64_t(input.readInt64());
    if (metadataBytes > 8 * 1024 * 1024 || metadataBytes > size - 25) {
        report.error = "Invalid metadata length.";
        return false;
    }
    juce::MemoryBlock metadata;
    input.readIntoMemoryBlock(metadata, size_t(metadataBytes));
    if (metadataBytes &&
        juce::JSON::parse(juce::String::fromUTF8(static_cast<const char *>(metadata.getData()),
                                                 int(metadata.getSize())),
                          doc.metadata)
            .failed()) {
        report.error = "Invalid JSON metadata.";
        return false;
    }
    int plain = input.readInt(), kind = input.readInt();
    if (plain <= 0 || size_t(plain) > limit || kind < 0 || kind > 2) {
        report.error =
            "Unsupported encoding or decoded length.";
        return false;
    }
    juce::MemoryBlock packed;
    input.readIntoMemoryBlock(packed, size_t(input.getNumBytesRemaining()));
    juce::MemoryBlock decoded{size_t(plain)};
    bool ok=false;
    if(kind==2){
        auto frame=ZSTD_findFrameCompressedSize(packed.getData(),packed.getSize());
        auto count=ZSTD_decompress(decoded.getData(),decoded.getSize(),packed.getData(),packed.getSize());
        if(ZSTD_isError(frame)||frame!=packed.getSize()||ZSTD_isError(count)||count!=decoded.getSize()){
            report.error="Invalid or truncated Zstandard payload.";return false;}
        ok=SerumCbor(decoded).read(doc.payload);
    }else{
        const auto *wire=static_cast<const uint8_t*>(packed.getData());size_t n=packed.getSize();
        if(n<6 || (wire[0]&15)!=8 || ((unsigned(wire[0])*256+wire[1])%31)!=0 || (wire[1]&32)){
            report.error="Invalid zlib header.";return false;}
        juce::MemoryInputStream compressed(packed,false);juce::GZIPDecompressorInputStream stream(compressed);
        int count=stream.read(decoded.getData(),plain);uint8_t extra=0;
        uint32_t a=1,b=0;for(size_t i=0;i<decoded.getSize();++i){a=(a+static_cast<const uint8_t*>(decoded.getData())[i])%65521;b=(b+a)%65521;}
        uint32_t checksum=(uint32_t(wire[n-4])<<24)|(uint32_t(wire[n-3])<<16)|(uint32_t(wire[n-2])<<8)|wire[n-1];
        if(count!=plain || stream.read(&extra,1)!=0 || checksum!=((b<<16)|a)){
            report.error="Invalid, oversized or truncated zlib payload.";return false;}
        if(kind==0)ok=juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(decoded.getData()),plain),doc.payload).wasOk();
        else ok=SerumCbor(decoded).read(doc.payload);
    }
    doc.encoding=kind;
    if (!ok || !doc.payload.isObject() || !doc.payload.hasProperty("Oscillator0")) {
        report.error = "Unsupported or malformed Serum preset schema.";
        return false;
    }
    report.name =
        doc.metadata
            .getProperty("presetName", doc.payload.getProperty("presetName", "Serum preset"))
            .toString();
    return true;
}

inline juce::var serumParams(const juce::var &node) { auto p=node.getProperty("plainParams", {});return p.isObject()?p:juce::var{}; }
inline double serumNumber(const juce::var &object, const char *key, double fallback) {
    auto v = object.getProperty(key, {});
    if (!(v.isDouble() || v.isInt() || v.isInt64() || v.isBool()))
        return fallback;
    double x = double(v);
    return std::isfinite(x) ? x : fallback;
}
struct SerumPlan {
    std::map<juce::String, float> values;
    std::array<juce::var, 3> oscillatorAssets, irAssets;
    std::array<int, 3> engines{};
    void set(const juce::String &id, double value) { values[id] = float(value); }
};
inline int serumWarp(const juce::var &value){
    if(value.isInt() || value.isInt64() || value.isDouble())return juce::jlimit(0,69,int(value));
    auto name=value.toString();if(name=="kOff" || name=="Off" || name.isEmpty())return 0;
    for(int i=0;i<70;++i)if(name==wavetableWarpEnums[i] || name==wavetableWarpNames[i])return i;return -1;
}
inline SerumPlan mapSerum(const SerumDocument &doc, SerumImportReport &report) {
    using namespace juce;
    SerumPlan plan;
    const auto &root = doc.payload;
    auto number = [](const var &obj, const char *key, double fallback) {
        return serumNumber(obj, key, fallback);
    };
    auto id = [](const char *prefix, int i, const char *suffix) {
        return String(prefix) + String(i) + "_" + suffix;
    };
    for (int i = 0; i < 3; ++i) {
        auto osc = root.getProperty(Identifier("Oscillator" + String(i)), {}),
             params = serumParams(osc);
        bool enabled = number(params, "kParamEnable", i == 0 ? 1 : 0) >= .5;
        double volume = enabled ? number(params, "kParamVolume", .75) : 0;
        String type = params.getProperty("kParamType", "kOsc_WT").toString();
        int mode = type.containsIgnoreCase("Granular")      ? 3
                   : type.containsIgnoreCase("MultiSample") ? 2
                   : type.containsIgnoreCase("Spectral")    ? 4
                   : type.containsIgnoreCase("Sample")      ? 1
                                                            : 0;
        plan.engines[size_t(i)] = mode;
        const char *prefix = mode == 0   ? "WTOsc"
                             : mode == 1 ? "SampleOsc"
                             : mode == 2 ? "MultiSampleOsc"
                             : mode == 3 ? "GranularOsc"
                                         : "SpectralOsc";
        auto asset = osc.getProperty(Identifier(String(prefix) + String(i)), {}),
             specific = serumParams(asset);
        plan.oscillatorAssets[size_t(i)] = asset;
        plan.set(id("osc", i, "mode"), mode);
        plan.set(id("osc", i, "level"), volume);
        double tune = number(params, "kParamOctave", 0) * 12 + number(params, "kParamPitch", 0) +
                      number(params, "kParamFine", 0) / 100;
        if (std::abs(tune) > 24)
            report.note("Oscillator tuning clamped to Aurora's +/-24 semitone range.");
        plan.set(id("osc", i, "tune"), tune);
        plan.set(id("osc", i, "pan"),number(params,"kParamPan",.5)*2-1);
        plan.set(id("osc", i, "unison"), number(params, "kParamUnison", 1));
        plan.set(id("osc", i, "detune"), number(params, "kParamDetune", .1) * 100);
        plan.set(id("osc", i, "phase"), number(specific, "kParamInitialPhase", 180) / 360);
        plan.set(id("osc", i, "random"), number(specific,"kParamRandomPhase",number(specific,"kParamRandPhase",100))/100);
        plan.set(id("osc", i, "position"), number(specific, "kParamTablePos", 0) / 256);
        plan.set(id("osc", i, "loop"), number(specific, "kParamOneShot", 0) < .5 ? 1 : 0);
        plan.set(id("osc",i,"wt_compat"),mode==0?1:0);
        plan.set(id("osc",i,"wt_enabled"),enabled?1:0);
        plan.set(id("osc",i,"wt_unison"),number(params,"kParamUnison",1));
        plan.set(id("osc",i,"wt_detune"),number(params,"kParamDetune",.5));
        plan.set(id("osc",i,"wt_blend"),number(params,"kParamDetuneWid",.75));
        plan.set(id("osc",i,"wt_width"),number(params,"kParamUnisonStereo",1));
        plan.set(id("osc",i,"wt_shape"),number(params,"kParamDetuneMode",0));
        plan.set(id("osc",i,"wt_stack"),number(params,"kParamUnisonStack",0));
        plan.set(id("osc",i,"wt_range"),number(params,"kParamUnisonRange",12));
        plan.set(id("osc",i,"wt_rotate"),4*number(params,"kParamUnisonRotate",.5)-2);
        plan.set(id("osc",i,"wt_variation"),2*number(specific,"kParamUnisonWTPos",.5)-1);
        plan.set(id("osc",i,"wt_xfade"),number(specific,"kParamXfadeMode",0));
        plan.set(id("osc",i,"wt_memory"),number(specific,"kParamPhaseMemory",0));
        plan.set(id("osc",i,"wt_legato"),number(specific,"kParamPhaseLegato",1));
        plan.set(id("osc",i,"wt_wrap"),number(specific,"kParamWrapPhase",1));
        for(int slot=0;slot<2;++slot){int warp=serumWarp(specific.getProperty(slot==0?"kParamWarpMenu":"kParamWarpMenu2","kNoWarp"));
            if(warp<0 || !supportedWavetableWarp(warp)){
                if(enabled && warp!=0)report.note("A warp mode requires an unavailable curve, filter or shaper; its data is preserved, but this slot is bypassed.");warp=0;}
            plan.set(id("osc",i,slot==0?"wt_warp1":"wt_warp2"),warp);
            plan.set(id("osc",i,slot==0?"wt_amount1":"wt_amount2"),number(specific,slot==0?"kParamWarp":"kParamWarp2",0));
        }
        plan.set(id("osc",i,"warp"),0);
    }
    for (int i = 3; i < 5; ++i) {
        auto params = serumParams(root.getProperty(Identifier("Oscillator" + String(i)), {}));
        plan.set(i == 3 ? "noise" : "sub",
                 number(params, "kParamEnable", 0) >= .5 ? number(params, "kParamVolume", .75) : 0);
        if (number(params, "kParamEnable", 0) >= .5)
            report.note(i == 3 ? "Noise uses Aurora white noise; sampled noise is not translated."
                               : "Sub uses Aurora's sine at -1 octave; source shape/octave "
                                 "differences are not reproduced.");
    }
    auto global = serumParams(root["Global0"]);
    plan.set("master", number(global, "kParamMasterVolume", .7));
    plan.set("voice_mode", number(global, "kParamMonoToggle", 0) >= .5 ? 1 : 0);
    plan.set("portamento", number(global, "kParamPortamento", 0) * 1000);
    for (int i = 0; i < 4; ++i) {
        auto env = serumParams(root.getProperty(Identifier("Env" + String(i)), {}));
        plan.set(id("env", i, "a"), number(env, "kParamAttack", .001));
        plan.set(id("env", i, "d"), number(env, "kParamDecay", 1));
        plan.set(id("env", i, "s"), number(env, "kParamSustain", 1));
        plan.set(id("env", i, "r"), number(env, "kParamRelease", .015));
    }
    for (int i = 0; i < 10; ++i) {
        auto lfo = root.getProperty(Identifier("LFO" + String(i)), {}), params = serumParams(lfo),
             curve = lfo["curveData"];
        auto xs = curve["xVals"], ys = curve["yVals"];
        if (xs.isArray() && ys.isArray() && xs.size() >= 2 && xs.size() == ys.size()) {
            int count = std::min(8, xs.size());
            plan.set(id("lfo", i, "shape"), 4);
            plan.set(id("lfo", i, "count"), count);
            for (int point = 0; point < count; ++point) {
                int index = roundToInt(point * double(xs.size() - 1) / (count - 1));
                plan.set(id("lfo", i, ("x" + String(point)).toRawUTF8()), double(xs[index]));
                plan.set(id("lfo", i, ("point" + String(point)).toRawUTF8()),
                         1 - double(ys[index]));
            }
            if (xs.size() > 8)
                report.note("LFO curves reduced to at most eight points.");
        }
        bool synced = number(params, "kParamBeatSync", 1) >= .5;
        plan.set(id("lfo", i, "sync"), synced ? 1 : 0);
        double rate = number(params, "kParamRate", synced ? 6.25 : 1);
        if (synced) {
            int step = roundToInt(std::pow(std::max(0., rate) / 100., .25) * 14);
            plan.set(id("lfo", i, "division"), jlimit(0, 6, step - 3));
            if (step < 3 || step > 9)
                report.note("Synced LFO divisions outside 4 bars..1/16 were clamped.");
        } else
            plan.set(id("lfo", i, "rate"), rate);
        plan.set(id("lfo", i, "phase"), number(params, "kParamPhase", 0) / 360);
        plan.set(id("lfo", i, "delay"), number(params, "kParamDelay", 0));
        plan.set(id("lfo", i, "rise"), number(params, "kParamRise", 0));
        if (number(params, "kParamDotted", 0) > 0 || number(params, "kParamTriplets", 0) > 0)
            report.note(
                "Dotted/triplet LFO timing is approximated by the nearest straight division.");
        String type = params.getProperty("kParamType", "").toString();
        if (type.isNotEmpty() && type != "LFO" && type != "Curve")
            report.note("Chaos and path LFO types use Aurora's basic LFO shape.");
    }
    for (int i = 0; i < 8; ++i)
        plan.set(id("macro", i, "value"),
                 number(serumParams(root.getProperty(Identifier("Macro" + String(i)), {})),
                        "kParamValue", 0) /
                     100);
    for (int i = 0; i < 2; ++i) {
        auto params = serumParams(root.getProperty(Identifier("VoiceFilter" + String(i)), {}));
        bool enabled = number(params, "kParamEnable", 0) >= .5;
        String type = params.getProperty("kParamType", "Low12").toString();
        int filterType = type.containsIgnoreCase("High")   ? 2
                         : type.containsIgnoreCase("Band") ? 1
                                                           : 0;
        double cutoff = number(params, "kParamFreq", 1);
        plan.set(id("filter", i, "type"), filterType);
        plan.set(id("filter", i, "cutoff"),
                 cutoff <= 1 ? 8 * std::pow(22050. / 8, cutoff) : cutoff);
        plan.set(id("filter", i, "res"), number(params, "kParamReso", 0) / 100);
        if (enabled)
            report.note(
                "Filters are approximated by Aurora SVFs; model, drive, keytrack and mix differ.");
    }
    bool filter0 = number(serumParams(root["VoiceFilter0"]), "kParamEnable", 0) >= .5;
    bool filter1 = number(serumParams(root["VoiceFilter1"]), "kParamEnable", 0) >= .5;
    for (int i = 0; i < 7; ++i) {
        auto route = serumParams(root.getProperty(Identifier("RoutingSlot" + String(i)), {}));
        String destinationName = route.getProperty("kParamRoutingDest", "").toString();
        double destination = destinationName.containsIgnoreCase("None")     ? .75
                             : destinationName.containsIgnoreCase("Direct") ? .5
                             : destinationName.containsIgnoreCase("Main")   ? .25
                             : i < 5                                        ? 0
                                                                            : .25;
        auto destinationValue=route.getProperty("kParamRoutingDest",{});
        if(destinationValue.isInt() || destinationValue.isInt64() || destinationValue.isDouble())destination=.25*juce::jlimit(0,3,int(destinationValue));
        if (destination == 0 && i < 5 && !filter0 && !filter1)
            destination = .25;
        plan.set(id("route", i, "dest"), destination);
        plan.set(id("route", i, "balance"),
                 number(route, "kParamFilterBalance", filter0 ? -100 : 100));
        plan.set(id("route", i, "bus1"), number(route, "kParamFXBus1Level", 0));
        plan.set(id("route", i, "bus2"), number(route, "kParamFXBus2Level", 0));
        plan.set(id("route", i, "env"), number(route, "kParamViaEnv1", i < 5 ? 1 : 0));
    }
    int slot = 0;
    for (int i = 0; i < 64; ++i) {
        auto route = root.getProperty(Identifier("ModSlot" + String(i)), {}),
             source = route["source"];
        if (!source.isArray() || source.size() < 1)
            continue;
        int s = int(source[0]), mapped = s == 1               ? 26
                                         : s >= 2 && s <= 5   ? s + 2
                                         : s >= 6 && s <= 15  ? s + 2
                                         : s == 16            ? 1
                                         : s == 17            ? 2
                                         : s == 18 || s == 19 ? 3
                                         : s >= 25 && s <= 32 ? s - 7
                                                              : 0;
        String type = route["destModuleTypeString"].toString(),
               name = route["destModuleParamName"].toString();
        int index = int(route["destModuleID"]), target = 0;
        int local=int(route.getProperty("destModuleParamID",-1));String expected;
        if(type=="Oscillator")expected=local==1?"kParamVolume":local==2?"kParamPan":local==4?"kParamPitch":"";
        if(type=="WTOsc" && local==6)expected="kParamTablePos";
        if(type=="VoiceFilter" && local==3)expected="kParamFreq";
        if(name.isEmpty())name=expected;
        else if(expected.isNotEmpty() && expected!=name){report.note("A local modulation parameter ID/name conflict was preserved and skipped; version-specific migration is required.");continue;}

        if (index >= 0 && index < 3 && type == "Oscillator")
            target = name == "kParamVolume"  ? 1 + index
                     : name == "kParamPan"   ? 8
                     : name == "kParamPitch" ? 7
                                             : 0;
        if (index >= 0 && index < 3 && type == "WTOsc" && name == "kParamTablePos")
            target = 4 + index;
        if (index >= 0 && index < 2 && type == "VoiceFilter" && name == "kParamFreq")
            target = 9 + index;
        if (!mapped || !target || (source.size() > 1 && int(source[1]) != 0)) {
            report.note("Some modulation sources, auxiliary sources or destinations are "
                        "unsupported and were skipped.");
            continue;
        }
        plan.set(id("mod", slot, "source"), mapped);
        plan.set(id("mod", slot, "target"), target);
        plan.set(id("mod", slot, "depth"), number(serumParams(route), "kParamAmount", 0) / 100);
        ++slot;
    }
    report.modulations = slot;
    const char *effects[]{"FXDistortion", "FXFlanger", "FXPhaser", "FXChorus", "FXDelay", "FXComp",
                          "FXReverb",     "FXEQ",      "FXFilter", "FXHyperD", "FXBode",  "FXConv"};
    for (int sourceRack = 0; sourceRack < 3; ++sourceRack) {
        int rack = sourceRack == 0 ? 2 : sourceRack - 1;
        auto fx = root.getProperty(Identifier("FXRack" + String(sourceRack)), {})["FX"];
        if (!fx.isArray())
            continue;
        if (fx.size() > 4)
            report.note("Only the first four effect slots per rack are translated.");
        for (int j = 0; j < std::min(4, fx.size()); ++j) {
            int type = 0;
            var node;
            for (int k = 0; k < 12; ++k)
                if (fx[j].hasProperty(effects[k])) {
                    type = k + 1;
                    node = fx[j][effects[k]];
                    break;
                }
            if (!type) {
                report.note("Unsupported effect types were skipped.");
                continue;
            }
            String prefix = "fx" + String(rack) + "s" + String(j) + "_";
            auto params = serumParams(node);
            plan.set(prefix + "type", type);
            plan.set(prefix + "mix", number(params, "kParamWet", 50) / 100);
            plan.set(prefix + "amount",
                     number(params, type == 5 ? "kParamFeedback" : "kParamDrive", 40) / 100);
            if (type == 5)
                plan.set(prefix + "time", (number(params, "kParamTimeL", .25) - .01) / .95);
            if (type == 12)
                plan.irAssets[size_t(rack)] = node;
            report.note(
                "Effect parameters and algorithms are approximations; rack topology may differ.");
        }
    }
    std::array<int,3> visiting{};
    auto visit=[&](auto &&self,int oscillator)->bool{if(visiting[size_t(oscillator)]==1)return false;if(visiting[size_t(oscillator)]==2)return true;visiting[size_t(oscillator)]=1;
        if(plan.values[id("osc",oscillator,"wt_enabled")]<.5f || plan.engines[size_t(oscillator)]!=0){visiting[size_t(oscillator)]=2;return true;}
        for(int slot=0;slot<2;++slot){int mode=int(plan.values[id("osc",oscillator,slot==0?"wt_warp1":"wt_warp2")]);
            if(mode>=33 && mode!=57){int source=mode>=58?(mode-58)%6:(mode-33)%6;
                if(source<2){int other=oscillator==0?source+1:oscillator==1?(source==0?0:2):source;if(!self(self,other))return false;}}}
        visiting[size_t(oscillator)]=2;return true;};
    bool acyclic=true;for(int oscillator=0;oscillator<3;++oscillator)acyclic &= visit(visit,oscillator);
    if(!acyclic){report.error="Cyclic external oscillator modulation is not supported safely by this renderer.";return plan;}
    for(auto key:{"noteOnExpressionCompat","loopEndCompat","roundingModeCompat","portaAlwaysTuningCompat","noteOffHandlingCompat","envLegatoInvertedCompat"})
        if(root.hasProperty(key))report.note("Version-specific compatibility flags were preserved; behavior outside implemented mappings may differ.");
    report.parameters = int(plan.values.size());
    report.note("Approximation: unavailable warp variants, envelope curvature, LFO retrigger modes, "
                "arp/clip patterns and advanced engine settings are not reproduced.");
    return plan;
}
} // namespace aurora
