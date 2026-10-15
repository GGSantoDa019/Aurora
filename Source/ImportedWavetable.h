#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <complex>
#include "WavetableReader.h"
namespace aurora {
struct ImportedWavetable {
    static constexpr int period=2048,bands=12,maxFrames=256,padding=8,frameStride=4287;
    std::vector<float> original,mipmaps;
    int frames=0;
    static int length(int band){return period>>band;}
    static int offset(int band){int at=0;for(int i=0;i<band;++i)at+=length(i)+16;return at;}
    const float *levelData(int frame,int band)const{return mipmaps.data()+frame*frameStride+offset(band)+padding;}
    static void fft(std::array<std::complex<double>,period>&data,bool inverse){
        for(int i=1,j=0;i<period;++i){int bit=period>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(data[size_t(i)],data[size_t(j)]);}
        for(int size=2;size<=period;size*=2){double angle=(inverse?2:-2)*3.14159265358979323846/size;
            std::complex<double> root(std::cos(angle),std::sin(angle));
            for(int start=0;start<period;start+=size){std::complex<double> w(1,0);
                for(int k=0;k<size/2;++k){auto x=data[size_t(start+k)],y=w*data[size_t(start+k+size/2)];
                    data[size_t(start+k)]=x+y;data[size_t(start+k+size/2)]=x-y;w*=root;}}}
        if(inverse)for(auto &v:data)v/=period;
    }
    bool build(std::vector<float> input){
        if(input.empty() || input.size()%period || input.size()>size_t(maxFrames*period))return false;
        for(float x:input)if(!std::isfinite(x)||std::abs(x)>16)return false;
        original=std::move(input);frames=int(original.size()/period);mipmaps.resize(size_t(frames*frameStride));
        std::array<std::complex<double>,period> spectrum{},filtered{};
        for(int frame=0;frame<frames;++frame){for(int i=0;i<period;++i)spectrum[size_t(i)]=original[size_t(frame*period+i)];fft(spectrum,false);spectrum[period/2]=0;
            for(int band=0;band<bands;++band){int n=length(band);auto *core=mipmaps.data()+frame*frameStride+offset(band)+padding;
                if(band==0)std::copy_n(original.data()+frame*period,period,core);
                else{int cutoff=band==11?1:n/2;
                    for(int i=0;i<period;++i)filtered[size_t(i)]=std::min(i,period-i)<=cutoff?spectrum[size_t(i)]:std::complex<double>{};
                    if(band==11)filtered[0]=0;fft(filtered,true);
                    for(int i=0;i<n;++i)core[i]=float(filtered[size_t(i*(1<<band))].real());}
                for(int i=0;i<8;++i){core[i-8]=core[(i-8)&(n-1)];core[n+i]=core[i&(n-1)];}
            }}return true;
    }
    static int selectLevel(float increment){
        if(!(increment>0) || !std::isfinite(increment))return 0;uint32_t bits;std::memcpy(&bits,&increment,4);
        return std::clamp(int((bits>>23)&255)+(bits&0x7fffff?1:0)+11-127,0,11);
    }
    float read(double phase,float position,double hz,double sampleRate,WavetableReadState *state=nullptr,bool morph=true,bool profile=false)const{
        if(frames<=0 || !std::isfinite(phase) || !(sampleRate>0) || hz>=sampleRate*.5)return 0;
        int band=selectLevel(float(std::abs(hz)/sampleRate));
        float frame=std::clamp(position,0.f,1.f)*float(profile?frames:frames-1);
        int first=std::clamp(int(frame),0,frames-1),second=std::min(frames-1,first+1);float blend=first==second?0:frame-first;
        phase-=std::floor(phase);
        auto atBand=[&](int level){int n=length(level);
            auto at=[&](int f){return WavetableReader::contiguous(levelData(f,level)-7,phase,n);};
            float a=at(first);return !morph || blend==0?a:a+(at(second)-a)*blend;};
        if(!state)return atBand(band);
        if(morph){state->frame=first;state->frameRemaining=0;return state->tick(this,band,atBand);}
        state->select(this,band);
        int levelRemaining=state->remaining;float value=0;
        if(!morph){
            if(state->frame<0){state->frame=state->oldFrame=first;state->frameRemaining=0;}
            if(!state->frameRemaining && state->frame!=first){state->oldFrame=state->frame;state->frame=first;state->frameRemaining=64;}
            auto readFrame=[&](int f){auto readLevel=[&](int level){return WavetableReader::contiguous(levelData(f,level)-7,phase,length(level));};
                int remaining=levelRemaining;float current=readLevel(state->band);
                return remaining>0?WavetableReader::crossfade(current,readLevel(state->previous),remaining):current;};
            value=readFrame(state->frame);
            if(state->frameRemaining)value=WavetableReader::crossfade(value,readFrame(state->oldFrame),state->frameRemaining);
        }
        if(state->remaining>0)--state->remaining;
        return value;
    }
};
}
