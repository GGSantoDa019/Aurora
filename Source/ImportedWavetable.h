#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

namespace aurora {
struct ImportedWavetable {
    static constexpr int period = 2048, bands = 8, maxFrames = 256;
    std::vector<float> original, mipmaps;
    int frames = 0;
    bool build(std::vector<float> input) {
        if (input.empty() || input.size() % period || input.size() > size_t(maxFrames * period))
            return false;
        for (float x : input)
            if (!std::isfinite(x) || std::abs(x) > 16)
                return false;
        original = std::move(input);
        frames = int(original.size() / period);
        mipmaps.resize(size_t(bands) * original.size());
        juce::dsp::FFT fft(11);
        std::array<float, period * 2> frequency{}, filtered{};
        for (int frame = 0; frame < frames; ++frame) {
            frequency.fill(0);
            std::copy_n(original.data() + frame * period, period, frequency.data());
            fft.performRealOnlyForwardTransform(frequency.data());
            for (int band = 0; band < bands; ++band) {
                filtered = frequency;
                int limit = (period / 2) >> band;
                for (int h = limit + 1; h <= period / 2; ++h)
                    filtered[size_t(h * 2)] = filtered[size_t(h * 2 + 1)] = 0;
                fft.performRealOnlyInverseTransform(filtered.data());
                std::copy_n(filtered.data(), period,
                            mipmaps.data() + (band * frames + frame) * period);
            }
        }
        return true;
    }
    float read(double phase, float position, double hz, double sampleRate) const {
        if (frames <= 0 || hz >= sampleRate * .45)
            return 0;
        int band = 0;
        while (band < bands - 1 && ((period / 2) >> band) * hz > sampleRate * .45)
            ++band;
        double p = phase - std::floor(phase), sample = p * period;
        int a = int(sample) & (period - 1), b = (a + 1) & (period - 1);
        float fraction = float(sample - std::floor(sample)),
              frame = juce::jlimit(0.f, 1.f, position) * (frames - 1);
        int first = int(frame), second = std::min(frames - 1, first + 1);
        auto at = [&](int f) {
            auto *wave = mipmaps.data() + (band * frames + f) * period;
            return wave[a] + fraction * (wave[b] - wave[a]);
        };
        return at(first) + (at(second) - at(first)) * (frame - first);
    }
};
} // namespace aurora
