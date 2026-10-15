#pragma once
#include "WavetableCoefficients.h"
#include <array>
#include <cmath>
#include <cstring>
#include <algorithm>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace aurora {
// Recovered leaf arithmetic only. Resource construction and band selection
// remain Aurora policies; this does not claim a complete Serum oscillator.
struct WavetableReader {
    struct Coefficients {
        std::array<float,8192> base{}, slope{};
        Coefficients() {
            std::memcpy(base.data(),wt_data::base,sizeof(wt_data::base));
            std::memcpy(slope.data(),wt_data::slope,sizeof(wt_data::slope));
        }
    };
    static const Coefficients &coefficients() {
        static const Coefficients c;
        return c;
    }
    static float contiguous(const float *samples, double phase, int period) {
        const double z=phase*(double(period)*512.0);
        const int index=int(z), q=index>>9, j=(index&511)*16;
        const float r=float(z-index);
        const auto &c=coefficients();
#if defined(__SSE2__) || defined(_M_X64)
        const auto fraction=_mm_set1_ps(r);
        auto term=[&](int k) {
            auto coefficient=_mm_add_ps(_mm_mul_ps(_mm_loadu_ps(c.slope.data()+j+k),fraction),
                                       _mm_loadu_ps(c.base.data()+j+k));
            return _mm_mul_ps(coefficient,_mm_loadu_ps(samples+q+k));
        };
        auto lanes=_mm_add_ps(term(4),term(0));
        lanes=_mm_add_ps(term(8),lanes);
        lanes=_mm_add_ps(term(12),lanes);
        alignas(16) float v[4];_mm_store_ps(v,lanes);
        volatile float a=v[3]+v[1],b=v[2]+v[0];
        return a+b;
#else
        float lanes[4];
        for(int lane=0;lane<4;++lane){
            float v[4];for(int k=0;k<4;++k){int t=lane+k*4;
                volatile float a=c.slope[size_t(j+t)]*r;
                volatile float b=a+c.base[size_t(j+t)];
                v[k]=b*samples[q+t];}
            volatile float a=v[1]+v[0],b=v[2]+a;lanes[lane]=v[3]+b;
        }
        volatile float a=lanes[3]+lanes[1],b=lanes[2]+lanes[0];return a+b;
#endif
    }
    static float periodic(const float *samples,int period,double phase) {
        phase-=std::floor(phase);
        const double z=phase*(double(period)*512.0);
        const int index=int(z),q=index>>9;
        // Original sample pointer has a seven-float pre-roll. Aurora's
        // equal-length bands use wrapped local taps instead of padded assets.
        if(q>=7 && q+8<period)return contiguous(samples+q-7,(z-index+(index&511))/512.0,1);
        float taps[16];for(int k=0;k<16;++k)taps[k]=samples[(q+k-7)&(period-1)];
        return contiguous(taps,(z-index+(index&511))/512.0,1);
    }
    static float crossfade(float current,float previous,int &remaining) {
        if(remaining<=0)return current;
        volatile float delta=previous-current;
        volatile float scaled=float(remaining)*delta;
        volatile float weighted=scaled*.015625f;
        --remaining;return current+weighted;
    }
};
struct WavetableReadState {
    const void *resource=nullptr;
    int band=-1,previous=-1,remaining=0;
    int frame=-1,oldFrame=-1,frameRemaining=0;
    void select(const void *identity,int next) {
        if(resource!=identity || band<0){resource=identity;band=previous=next;remaining=0;frame=-1;frameRemaining=0;}
        else if(!remaining && band!=next){previous=band;band=next;remaining=64;}
    }
    template<class Read> float tick(const void *identity,int next,Read read) {
        select(identity,next);float value=read(band);
        return remaining>0?WavetableReader::crossfade(value,read(previous),remaining):value;
    }
};
}
