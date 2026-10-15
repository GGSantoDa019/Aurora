#pragma once
#include "WavetableIntegrationFixture.h"
#include "WavetableAlgorithms.h"
#include "ImportedWavetable.h"
inline void wavetableIntegrationTests(const std::function<void(bool,const char*)> &check){
    using namespace aurora;
    auto fromBits=[](uint32_t bits){float v;std::memcpy(&v,&bits,4);return v;};
    auto identical=[](float v,uint32_t bits){uint32_t actual;std::memcpy(&actual,&v,4);return actual==bits;};
    const uint32_t *inputs[]{wt_evidence::impulse_input,wt_evidence::sine_input,wt_evidence::harmonics_input,wt_evidence::ramp_input};
    const uint32_t *levels[]{wt_evidence::impulse_levels,wt_evidence::sine_levels,wt_evidence::harmonics_levels,wt_evidence::ramp_levels};
    bool builder=true;double worst=0;
    for(int test=0;test<4;++test){std::vector<float> source(2048);for(int i=0;i<2048;++i)source[size_t(i)]=fromBits(inputs[test][i]);
        ImportedWavetable table;builder &= table.build(source) && table.mipmaps.size()==4287;
        for(size_t i=0;i<table.mipmaps.size();++i)worst=std::max(worst,std::abs(double(table.mipmaps[i])-fromBits(levels[test][i])));
        builder &= std::memcmp(table.levelData(0,0),source.data(),8192)==0;
        for(int band=0;band<12;++band){auto *core=table.levelData(0,band);int n=table.length(band);
            for(int i=0;i<8;++i)builder &= core[i-8]==core[(i-8)&(n-1)] && core[n+i]==core[i&(n-1)];}
    }
    check(builder && worst<=1.e-6,"Compact periodic wavetable builder: 48 captured levels / exact level zero / circular guards / error <= 1e-6");
    bool selection=true;
    for(int exponent=-12;exponent<=0;++exponent){float step=std::ldexp(1.f,exponent);
        for(float x:{std::nextafter(step,0.f),step,std::nextafter(step,2.f)}){
            uint32_t bits;std::memcpy(&bits,&x,4);int expected=std::clamp(int((bits>>23)&255)+(bits&0x7fffff?1:0)+11-127,0,11);
            selection &= ImportedWavetable::selectLevel(x)==expected;}}
    int identity;WavetableReadState state;state.select(&identity,0);state.select(&identity,1);
    for(int i=0;i<64;++i){state.select(&identity,4);selection &= state.band==1;state.tick(&identity,4,[](int n){return float(n);});}
    state.select(&identity,4);selection &= state.band==4 && state.remaining==64;
    check(selection,"IEEE level boundaries and deferred new level requests preserve the active 64-evaluation transition");
    bool pan=true;size_t at=0;
    for(int n=1;n<=16;++n)for(float blend:{0.f,.1f,.5f,.75f,1.f})for(float width:{0.f,.1f,.5f,.75f,1.f}){
        auto gains=wavetablePan(n,blend,width);for(int i=0;i<n*2;++i){pan &= identical(gains[size_t(i)],wt_evidence::pan[at]);at+=2;}}
    check(pan && at==13600,"Unison pan: 6800 float outputs match the captured native helper bit-for-bit");
    bool random=true;at=0;for(uint64_t seed:{5489ull,0ull,1ull,0xffffffffffffffffull}){PhaseRandom rng(seed);
        for(int i=0;i<2500;++i)random &= rng.next()==wt_evidence::random[at++];}
    check(random,"Seeded MT19937-64: 10000 reference outputs agree with the native-validated sequence");
    bool phase=true;PhaseRandom rng;at=0;
    for(double base:{0.,.25,.9})for(double amount:{0.,.25,.5,1.})for(double explicitPhase:{-1.,.3})for(bool reuse:{false,true})for(unsigned member=0;member<16;++member){
        int64_t stored=int64_t((.125+member/32.)*double(1ull<<43));double value=rng.initialPhase(base,amount,.37f,member,explicitPhase,reuse,stored);
        uint64_t bits;std::memcpy(&bits,&value,8);phase &= bits==wt_evidence::phase[at++];}
    check(phase,"Phase initialization / explicit / stored reuse: 768 captured double outputs match bit-for-bit");
    bool external=true;at=0;
    for(int a=0;a<7;++a)for(int b=0;b<7;++b)for(bool noise:{false,true})for(float amount:{0.f,.125f,.5f,1.f})for(float sample:{-1.f,-.5f,0.f,.25f,1.f}){
        ExternalWarp scratch;scratch.apply(0,a,amount,sample,noise);scratch.apply(1,b,.3f,-.75f,noise);
        for(int i=0;i<4;++i)external &= identical(scratch.value[size_t(i)],wt_evidence::external[at+size_t((i+1)*2)]);at+=14;}
    check(external && at==27440,"Two external warp slots: captured FM / exponential / PD / AM / RM coefficients match bit-for-bit");
    ImportedWavetable table;std::vector<float> frames(4096);std::fill(frames.begin()+2048,frames.end(),1.f);table.build(frames);
    WavetableReadState discrete;float first=table.read(.25,0,220,48000,&discrete,false,true);
    float start=table.read(.25,.75f,220,48000,&discrete,false,true);
    for(int i=0;i<64;++i)table.read(.25,.75f,220,48000,&discrete,false,true);
    float end=table.read(.25,.75f,220,48000,&discrete,false,true),morph=table.read(.25,.25f,220,48000,nullptr,true,true);
    check(first==0 && start==0 && std::abs(end-1.00317f)<.001 && std::abs(morph-.501585f)<.001,"Discrete frame changes crossfade while morph mode interpolates adjacent frames");
}
