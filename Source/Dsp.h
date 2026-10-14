#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>
namespace aurora {
struct WaveTap {
    juce::AbstractFifo fifo{4096};
    std::array<float, 4096> samples{};
    std::atomic<float> peak{0};
    void push(float value) {
        int a, b, c, d;
        fifo.prepareToWrite(1, a, b, c, d);
        if (b) {
            samples[size_t(a)] = value;
            fifo.finishedWrite(1);
        }
    }
    int pull(float *values, int capacity) {
        int a, b, c, d;
        fifo.prepareToRead(capacity, a, b, c, d);
        for (int i = 0; i < b; ++i)
            values[i] = samples[size_t(a + i)];
        for (int i = 0; i < d; ++i)
            values[b + i] = samples[size_t(c + i)];
        fifo.finishedRead(b + d);
        return b + d;
    }
};
constexpr float pi = 3.14159265358979323846f;
inline float nonlinear(float x) {
    x = juce::jlimit(-1.5f, 1.5f, x);
    return x - 0.14814815f * x * x * x;
}
struct Wavetables {
    std::array<std::array<std::array<float, 2048>, 4>, 8> data{};
    Wavetables() {
        for (int level = 0; level < 8; ++level)
            for (int i = 0; i < 2048; ++i) {
                float phase = 2 * pi * i / 2048.f;
                data[size_t(level)][0][size_t(i)] = std::sin(phase);
                for (int h = 1; h <= (128 >> level); ++h) {
                    float s = std::sin(phase * h);
                    data[size_t(level)][1][size_t(i)] += s * .55f / h;
                    if (h % 2) {
                        data[size_t(level)][2][size_t(i)] += s * .8f / h;
                        data[size_t(level)][3][size_t(i)] +=
                            s * ((h % 4 == 1) ? 1.f : -1.f) * .8f / (h * h);
                    }
                }
            }
    }
    float read(double phase, float position, double hz, double sr) const {
        if (hz >= sr * .45)
            return 0;
        int level = 0;
        while (level < 7 && (128 >> level) * hz > sr * .45)
            ++level;
        float frame = position * 3;
        int a = juce::jlimit(0, 3, int(frame)), b = std::min(3, a + 1);
        float blend = frame - a;
        float at = float(phase) * 2048;
        int index = int(at) & 2047, next = (index + 1) & 2047;
        float frac = at - std::floor(at);
        auto interp = [&](int f) {
            const auto &t = data[size_t(level)][size_t(f)];
            return t[size_t(index)] + frac * (t[size_t(next)] - t[size_t(index)]);
        };
        return interp(a) + (interp(b) - interp(a)) * blend;
    }
};
struct SVF {
    float s1 = 0, s2 = 0;
    float tick(float x, float hz, float resonance, double sr, int mode = 0) {
        const float g = std::tan(pi * juce::jlimit(10.f, float(sr * .45), hz) / float(sr));
        const float k = 2.f - 1.9f * juce::jlimit(0.f, 1.f, resonance), a = 1.f / (1 + g * (g + k));
        const float v1 = a * (s1 + g * (nonlinear(x) - s2)), v2 = s2 + g * v1;
        s1 = 2 * v1 - s1;
        s2 = 2 * v2 - s2;
        if (!std::isfinite(s1) || !std::isfinite(s2))
            s1 = s2 = 0;
        return mode == 1 ? v1 : mode == 2 ? nonlinear(x) - k * v1 - v2 : v2;
    }
};
struct Delay {
    std::vector<float> data;
    int pos = 0;
    void prepare(double sr) {
        data.assign(size_t(sr * 3) + 8, 0);
        pos = 0;
    }
    float read(float samples) const {
        if (data.empty())
            return 0;
        float p = float(pos) - juce::jlimit(1.f, float(data.size() - 2), samples);
        if (p < 0)
            p += float(data.size());
        int i = int(p);
        float f = p - i;
        return data[size_t(i)] * (1 - f) + data[size_t((i + 1) % int(data.size()))] * f;
    }
    void push(float x) {
        data[size_t(pos)] = x;
        if (++pos == int(data.size()))
            pos = 0;
    }
};
struct Allpass {
    float state = 0;
    float tick(float x, float a) {
        float t = a * (x + state), y = state + t;
        state = x - t;
        return y;
    }
};
struct Effect {
    double sr = 48000;
    Delay dl, dr;
    SVF fl, fr, fl2, fr2;
    juce::Reverb rev;
    juce::dsp::Convolution conv;
    juce::AudioBuffer<float> scratch;
    std::array<Allpass, 19> al{}, ar{};
    float phase = 0, env = 0, mixSmooth = 0;
    int previous = 0;
    void prepare(double rate, int block) {
        sr = rate;
        dl.prepare(sr);
        dr.prepare(sr);
        fl = {};
        fr = {};
        fl2 = {};
        fr2 = {};
        al = {};
        ar = {};
        phase = env = 0;
        rev.setSampleRate(sr);
        rev.reset();
        conv.prepare({sr, juce::uint32(block), 2});
        scratch.setSize(2, block);
    }
    void loadIR(const juce::File &f) {
        conv.loadImpulseResponse(f, juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::yes, 0);
    }
    void process(float *l, float *r, int n, int type, float amount, float mix, float time) {
        if (type == 0)
            return;
        if (type != previous) {
            fl = {};
            fr = {};
            fl2 = {};
            fr2 = {};
            al = {};
            ar = {};
            rev.reset();
            env = 0;
            previous = type;
        }
        if (type == 12) {
            if (n > scratch.getNumSamples())
                return;
            scratch.copyFrom(0, 0, l, n);
            scratch.copyFrom(1, 0, r, n);
            auto block = juce::dsp::AudioBlock<float>(scratch).getSubBlock(0, size_t(n));
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            conv.process(ctx);
            for (int i = 0; i < n; ++i) {
                mixSmooth += (mix - mixSmooth) * .002f;
                l[i] += mixSmooth * (scratch.getSample(0, i) - l[i]);
                r[i] += mixSmooth * (scratch.getSample(1, i) - r[i]);
            }
            return;
        }
        if (type == 7) {
            juce::Reverb::Parameters p;
            p.roomSize = amount;
            p.damping = .45f;
            p.wetLevel = 1;
            p.dryLevel = 0;
            p.width = 1;
            rev.setParameters(p);
            scratch.copyFrom(0, 0, l, n);
            scratch.copyFrom(1, 0, r, n);
            rev.processStereo(scratch.getWritePointer(0), scratch.getWritePointer(1), n);
            for (int i = 0; i < n; ++i) {
                mixSmooth += (mix - mixSmooth) * .002f;
                l[i] += mixSmooth * (scratch.getSample(0, i) - l[i]);
                r[i] += mixSmooth * (scratch.getSample(1, i) - r[i]);
            }
            return;
        }
        for (int i = 0; i < n; ++i) {
            float x = l[i], y = r[i], wl = x, wr = y;
            phase += float((type == 11 ? (time - .5f) * 2000 : (.05 + time * 4)) / sr);
            phase -= std::floor(phase);
            const float tri = 1 - 4 * std::abs(phase - .5f);
            switch (type) {
            case 1:
                wl = std::tanh(x * (1 + amount * 20));
                wr = std::tanh(y * (1 + amount * 20));
                break;
            case 2:
            case 4:
            case 10: {
                const int taps = type == 10 ? 4 : 1;
                wl = wr = 0;
                for (int j = 0; j < taps; ++j) {
                    float p = phase + j * .25f;
                    p -= std::floor(p);
                    float t = 1 - 4 * std::abs(p - .5f);
                    float base = type == 2 ? 2.f : 15.f;
                    wl += dl.read(float(sr * .001) * (base + amount * 6 * (t + 1)));
                    wr += dr.read(float(sr * .001) * (base + amount * 6 * (1 - t)));
                }
                wl /= float(taps);
                wr /= float(taps);
                float inL = x, inR = y;
                if (type == 10) {
                    inL = fl.tick(inL, 12000, .2f, sr);
                    inL = fl2.tick(inL, 12000, .2f, sr);
                    inR = fr.tick(inR, 12000, .2f, sr);
                    inR = fr2.tick(inR, 12000, .2f, sr);
                }
                dl.push(inL + (type == 2 ? wl * amount * .6f : 0));
                dr.push(inR + (type == 2 ? wr * amount * .6f : 0));
                break;
            }
            case 3: {
                float a = .1f + .75f * (tri + 1) * .5f;
                for (int j = 0; j < 6; ++j) {
                    wl = al[size_t(j)].tick(wl, a);
                    wr = ar[size_t(j)].tick(wr, a);
                }
                break;
            }
            case 5:
                wl = dl.read(float(sr * (.01 + time * 1.99)));
                wr = dr.read(float(sr * (.013 + time * 1.97)));
                dl.push(x + wr * amount * .85f);
                dr.push(y + wl * amount * .85f);
                break;
            case 6:
                env += (std::max(std::abs(x), std::abs(y)) - env) *
                       float(1 - std::exp(-1 / (sr * .025)));
                {
                    float threshold = .05f + .7f * (1 - amount);
                    float gain = env > threshold ? std::pow(threshold / env, .75f) : 1;
                    wl = x * gain;
                    wr = y * gain;
                }
                break;
            case 8: {
                float lowL = fl.tick(x, 800, .1f, sr), lowR = fr.tick(y, 800, .1f, sr);
                float gain = std::pow(10.f, (amount - .5f) * 1.2f);
                wl = lowL * gain + (x - lowL) / gain;
                wr = lowR * gain + (y - lowR) / gain;
                break;
            }
            case 9:
                wl = fl.tick(x, 30 * std::pow(600.f, time), amount, sr);
                wr = fr.tick(y, 30 * std::pow(600.f, time), amount, sr);
                break;
            case 11: {
                // Independent analytic-signal frequency shifter. The supplied all-pass
                // recurrence is used as a phase disperser; a windowed Hilbert FIR supplies
                // quadrature because the original coefficient generator was not recovered.
                for (int j = 0; j < 18; ++j) {
                    float a = .05f + .9f * float(j + 1) / 19 * amount;
                    wl = al[size_t(j)].tick(wl, a);
                    wr = ar[size_t(j)].tick(wr, a);
                }
                wl = al[18].tick(wl, .45f);
                wr = ar[18].tick(wr, .45f);
                dl.push(wl);
                dr.push(wr);
                float ql = 0, qr = 0;
                for (int k = -63; k <= 63; k += 2) {
                    float coeff = 2 / (pi * k) * (.54f + .46f * std::cos(pi * k / 64));
                    ql += coeff * dl.read(float(64 + k));
                    qr += coeff * dr.read(float(64 + k));
                }
                float c = std::cos(2 * pi * phase), s = std::sin(2 * pi * phase);
                wl = dl.read(64) * c - ql * s;
                wr = dr.read(64) * c - qr * s;
                break;
            }
            case 13: {
                float m = (x + y) * .5f, s = (x - y) * .5f * (amount * 2);
                wl = m + s;
                wr = m - s;
                break;
            }
            case 14: {
                float a = dl.read(float(sr * .007)), b = dl.read(float(sr * .011)),
                      c = dr.read(float(sr * .013)), d = dr.read(float(sr * .017));
                dl.push(x);
                dr.push(y);
                float side = (a - b + c - d) * amount * .5f;
                wl = x + side;
                wr = y - side;
                break;
            }
            case 15: {
                float m = (x + y) * .5f, s = (x - y) * .5f;
                wl = m + s * amount * 2;
                wr = m - s * amount * 2;
                break;
            }
            case 16: {
                float lo = fl.tick(x, 600, .1f, sr), ro = fr.tick(y, 600, .1f, sr);
                wl = lo * amount * 2 + (x - lo) * (2 - amount * 2);
                wr = ro * amount * 2 + (y - ro) * (2 - amount * 2);
                break;
            }
            case 17: {
                float lo = fl.tick(x, 300, .1f, sr), ro = fr.tick(y, 300, .1f, sr),
                      ml = fl2.tick(x, 3000, .1f, sr), mr = fr2.tick(y, 3000, .1f, sr);
                wl = lo + (ml - lo) * amount * 2 + (x - ml) * (2 - amount * 2);
                wr = ro + (mr - ro) * amount * 2 + (y - mr) * (2 - amount * 2);
                break;
            }
            }
            mixSmooth += (mix - mixSmooth) * .002f;
            l[i] = x + mixSmooth * (wl - x);
            r[i] = y + mixSmooth * (wr - y);
            if (!std::isfinite(l[i]))
                l[i] = 0;
            if (!std::isfinite(r[i]))
                r[i] = 0;
        }
    }
};
} // namespace aurora
