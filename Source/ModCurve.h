#pragma once
#include <algorithm>
#include <array>
namespace aurora {
struct CurveShape {
    int count = 8;
    float tension = 0;
    std::array<float, 8> x{0, 1.f / 7, 2.f / 7, 3.f / 7, 4.f / 7, 5.f / 7, 6.f / 7, 1},
        y{0, 1, .8f, .65f, .5f, .3f, .15f, 0};
    float at(float phase) const {
        phase = std::max(0.f, std::min(1.f, phase));
        int segment = 0;
        while (segment < count - 2 && phase > x[size_t(segment + 1)])
            ++segment;
        float t = std::max(
            0.f, std::min(1.f, (phase - x[size_t(segment)]) /
                                   std::max(.001f, x[size_t(segment + 1)] - x[size_t(segment)])));
        float a = y[size_t(segment)], b = y[size_t(segment + 1)], delta = b - a;
        float c1 = a + delta * (1.f / 3 + tension * .3f),
              c2 = a + delta * (2.f / 3 + tension * .3f), u = 1 - t;
        return std::max(0.f, std::min(1.f, u * u * u * a + 3 * u * u * t * c1 + 3 * u * t * t * c2 +
                                               t * t * t * b));
    }
};
struct DisplayFrame {
    std::array<float, 256> samples{};
    float position = 0;
    int mode = 0;
};
struct DisplayQueue {
    juce::AbstractFifo fifo{4};
    std::array<DisplayFrame, 4> frames;
    void push(const DisplayFrame &value) {
        int a, b, c, d;
        fifo.prepareToWrite(1, a, b, c, d);
        if (b) {
            frames[size_t(a)] = value;
            fifo.finishedWrite(1);
        }
    }
    bool latest(DisplayFrame &value) {
        bool got = false;
        int a, b, c, d;
        fifo.prepareToRead(4, a, b, c, d);
        for (int i = 0; i < b; ++i) {
            value = frames[size_t(a + i)];
            got = true;
        }
        for (int i = 0; i < d; ++i) {
            value = frames[size_t(c + i)];
            got = true;
        }
        fifo.finishedRead(b + d);
        return got;
    }
};
} // namespace aurora
