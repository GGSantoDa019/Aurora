#pragma once
#include <JuceHeader.h>
#include <array>
#include <algorithm>
#include <cmath>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif
namespace aurora {
struct NumericEnvironment {
#if defined(__SSE__) || defined(_M_X64)
    unsigned saved = _mm_getcsr();
    NumericEnvironment() { _mm_setcsr(saved | 0x8040u); }
    ~NumericEnvironment() { _mm_setcsr(saved); }
#else
    juce::ScopedNoDenormals guard;
#endif
};
struct LinearParameter {
    float current = 0, target = 0, step = 0;
    int remaining = 0;
    void reset(float v) { current = target = v; step = 0; remaining = 0; }
    void set(float v, int samples) {
        if (v == target) return;
        target = v; remaining = std::max(0, samples);
        step = remaining ? (v - current) / remaining : 0;
        if (!remaining) current = target;
    }
    float next() {
        if (remaining && --remaining == 0) current = target;
        else if (remaining) current += step;
        return current;
    }
};
struct RoutingDescriptor {
    static float plain(int local, float n) {
        n = std::isfinite(n) ? juce::jlimit(0.f, 1.f, n) : 0;
        return local == 0 ? 200*n-100 : local < 3 ? 100*n
             : local == 3 ? float(std::min(3, int(4*n))) : float(n >= .5f);
    }
    static float normalized(int local, float v) {
        return juce::jlimit(0.f, 1.f, local == 0 ? (v+100)/200 : local < 3 ? v/100
                                      : local == 3 ? v/3 : v);
    }
    static unsigned canonical(int instance, int local) { return 109000000u + 1000u*instance + local; }
};
// Domain separation: a canonical foreign module ID is never an Aurora host ID.
class RoutingDestinationParameter : public juce::AudioParameterFloat {
    static juce::NormalisableRange<float> range(){
        return {0.f,3.f,
            [](float,float,float n){return RoutingDescriptor::plain(3,n);},
            [](float,float,float p){return RoutingDescriptor::normalized(3,p);},
            [](float,float,float p){return std::round(juce::jlimit(0.f,3.f,p));}};
    }
public:
    RoutingDestinationParameter(const juce::String&id,const juce::String&name)
        : juce::AudioParameterFloat(juce::ParameterID(id,1),name,range(),1.f){}
    int getNumSteps() const override{return 4;}
    bool isDiscrete() const override{return true;}
};
struct ParameterDescriptor {
    juce::String auroraId, name, unit;
    float minimum = 0, maximum = 1, defaultValue = 0;
    juce::uint32 canonicalId = 0; // 0 means unmapped (Global 0 is not assigned here).
};
struct AutomationPoint { int parameter = 0, sampleOffset = 0; float plain = 0; unsigned serial = 0; };
struct AutomationQueue {
    static constexpr int capacity = 8192;
    std::array<AutomationPoint, capacity> points{};
    int count = 0;
    unsigned serial = 0;
    std::atomic<unsigned> rejected{0};
    // Producer and consumer run on the audio callback thread; no cross-thread writes.
    bool push(int parameter, int offset, float plain) {
        if (parameter < 0 || offset < 0 || !std::isfinite(plain) || count == capacity) {
            rejected.fetch_add(1, std::memory_order_relaxed); return false;
        }
        points[size_t(count++)] = {parameter, offset, plain, serial++}; return true;
    }
    void sort() {
        std::sort(points.begin(), points.begin()+count, [](const auto &a, const auto &b) {
            return a.sampleOffset == b.sampleOffset ? a.serial < b.serial : a.sampleOffset < b.sampleOffset;
        });
    }
};
inline void midSide4(const float *l, const float *r, float *m, float *s, int n) {
    int i = 0;
#if defined(__SSE__) || defined(_M_X64)
    const auto half = _mm_set1_ps(.5f);
    for (; i+4 <= n; i+=4) {
        auto a = _mm_loadu_ps(l+i), b = _mm_loadu_ps(r+i);
        _mm_storeu_ps(m+i, _mm_mul_ps(_mm_add_ps(a,b),half));
        _mm_storeu_ps(s+i, _mm_mul_ps(_mm_sub_ps(a,b),half));
    }
#endif
    for (; i<n; ++i) { float a=l[i], b=r[i]; m[i]=(a+b)*.5f; s[i]=(a-b)*.5f; }
}
}
