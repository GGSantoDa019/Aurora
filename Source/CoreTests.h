#pragma once
#include "Dsp.h"
#include "Preset.h"
#include <set>
#include "WavetableFixture.h"
inline void coreInfrastructureTests(const std::function<void(bool,const char*)> &check){
    using namespace aurora;
    auto decodeBits=[](uint32_t bits){float v;std::memcpy(&v,&bits,4);return v;};
    auto sameBits=[](float value,uint32_t bits){uint32_t actual;std::memcpy(&actual,&value,4);return actual==bits;};
    std::array<float,2064> inputA{},inputB{};
    for(int i=0;i<2064;++i){inputA[size_t(i)]=decodeBits(wt_fixture::a[i]);inputB[size_t(i)]=decodeBits(wt_fixture::b[i]);}
    bool readerExact=true;
    for(int i=0;i<1028;++i){float v=WavetableReader::contiguous(inputA.data(),wt_fixture::phases[i],2048);
        readerExact &= sameBits(v,wt_fixture::pairs[i*2]);}
    check(readerExact,"Wavetable 16-tap / 512-phase reader matches 1028 captured native outputs bit-for-bit");
    int remaining=64;bool transitionExact=true;
    for(int i=0;i<64;++i){double phase=wt_fixture::phases[i];
        float v=WavetableReader::crossfade(WavetableReader::contiguous(inputA.data(),phase,2048),
            WavetableReader::contiguous(inputB.data(),phase,2048),remaining);
        transitionExact &= sameBits(v,wt_fixture::pairs[(1028+i)*2]) && remaining==63-i;
    }
    check(transitionExact,"Wavetable 64-evaluation crossfade matches captured native outputs bit-for-bit");
    WavetableReadState readerState;int identity=0,other=0;
    auto constant=[](int band){return float(band);};
    bool lifecycle=readerState.tick(&identity,0,constant)==0;
    lifecycle &= readerState.tick(&identity,1,constant)==0 && readerState.remaining==63;
    for(int i=0;i<63;++i)readerState.tick(&identity,1,constant);
    lifecycle &= readerState.remaining==0 && readerState.tick(&identity,1,constant)==1;
    lifecycle &= readerState.tick(&other,2,constant)==2 && readerState.remaining==0;
    check(lifecycle,"Wavetable per-reader level transition settles in 64 evaluations and resets on asset change");
    std::array<float,2048> periodic{};periodic.fill(.5f);bool wrapped=true;
    for(double phase:{-1.0,-.001,0.0,.125,.999999,1.0,3.125})
        wrapped &= std::isfinite(WavetableReader::periodic(periodic.data(),2048,phase)) &&
            std::abs(WavetableReader::periodic(periodic.data(),2048,phase)-.501584f)<.00002f;
    check(wrapped,"Wavetable periodic taps safely wrap negative and boundary phases with recovered DC gain");

    LinearParameter ramp;ramp.reset(0);ramp.set(1,480);bool linear=true;
    for(int i=1;i<=480;++i)linear &= std::abs(ramp.next()-i/480.f)<1.e-5f;
    linear &= ramp.remaining==0 && ramp.current==1;
    ramp.set(0,480);for(int i=0;i<73;++i)ramp.next();float interrupted=ramp.current;ramp.set(.5f,480);
    linear &= std::abs(ramp.next()-(interrupted+(.5f-interrupted)/480))<1.e-6f;
    check(linear,"10ms linear smoothing reaches target exactly and handles interrupted ramps");
    bool routing=true;
    for(int slot=0;slot<7;++slot){
        routing &= RoutingDescriptor::canonical(slot,4)==109000004u+unsigned(slot)*1000;
        for(int k=1;k<=3;++k){float edge=k*.25f;routing &= RoutingDescriptor::plain(3,std::nextafter(edge,0.f))==k-1 && RoutingDescriptor::plain(3,edge)==k;}
    }
    routing &= RoutingDescriptor::plain(0,0)==-100 && RoutingDescriptor::plain(0,1)==100 && RoutingDescriptor::plain(1,.5)==50 && RoutingDescriptor::plain(2,1)==100 && RoutingDescriptor::plain(4,std::nextafter(.5f,0.f))==0 && RoutingDescriptor::plain(4,.5)==1;
    check(routing,"7 canonical RoutingSlots / plain ranges / exact destination and Env1 boundary quantization");
    bool polynomial=true;for(float x:{-4.f,-1.5f,-.7f,0.f,.5f,1.5f,4.f}){float c=juce::jlimit(-1.5f,1.5f,x);polynomial &= std::abs(nonlinear(x)-(c-.14814815f*c*c*c))<1.e-7f;}
    check(polynomial,"SVF exact clamped cubic x - 0.14814815*x^3");
    Ring8192 ring;for(int i=0;i<20000;++i)ring.push(float(i));
    check(ring.cursor==(20000u&0x1fff) && ring.read(1)==19999 && ring.read(1.5)==19998.5f && ring.read(8190)==11810,"8192 ring mask / wrap / fractional read");
    CompressorDetector detector;double expected=0;bool detectorOK=true;
    for(int i=0;i<20000;++i){float x=std::sin(i*.07f);expected+=(std::abs(x)-expected)*.00025;detectorOK &= std::abs(detector.tick(x)-expected)<2.e-6;}
    check(detectorOK,"Compressor detector recurrence coefficient exactly 0.00025");
    BodePhasePair bode;for(int i=0;i<37;++i)bode.coefficient[size_t(i)]=.1+.8*i/37;
    std::array<double,37> states{};bool pairOK=true;
    auto section=[](double x,double a,double&s){double t=a*(x+s),y=s+t;s=x-t;return y;};
    for(int i=0;i<1024;++i){double x=std::sin(i*.19),a=x,b=section(x,bode.coefficient[36],states[36]);
        for(int j=0;j<18;++j)a=section(a,bode.coefficient[size_t(j)],states[size_t(j)]);
        for(int j=18;j<36;++j)b=section(b,bode.coefficient[size_t(j)],states[size_t(j)]);
        auto value=bode.tick(x);pairOK &= std::abs(value[0]-a)<1.e-12 && std::abs(value[1]+b)<1.e-12;}
    check(pairOK,"Bode two independent 18-section chains / extra section / second-output sign");
    bool convolution=true;
    for(int length:{1,17,127,128,129,257,509}){
        std::vector<float> ir(size_t(length),0.f),input(2048),output(2048);
        for(int j=0;j<length;++j)ir[size_t(j)]=std::cos(j*.13f)*std::exp(-j*.03f)*.2f;
        for(int j=0;j<2048;++j)input[size_t(j)]=std::sin(j*.21f)*.3f;
        PartitionedFIR fir;fir.configure(ir);convolution &= fir.productGain==1.f/256;
        for(int i=0;i<2048;++i){float expected=0;for(int k=0;k<length && k<=i;++k)expected+=ir[size_t(k)]*input[size_t(i-k)];
            convolution &= std::abs(fir.tick(input[size_t(i)])-expected)<3.e-5f;}
    }
    check(convolution,"FIR direct head + partitioned FFT overlap-add equals direct reference / 1-Nfft gain / non-power-of-two IR lengths");
    float l[7]{1,-1,0,.3f,.5f,.7f,1},r[7]{-1,1,0,.3f,-.5f,-.7f,-1},m[7],side[7];midSide4(l,r,m,side,7);bool ms=true;
    for(int i=0;i<7;++i)ms &= std::abs(m[i]+side[i]-l[i])<1.e-6f && std::abs(m[i]-side[i]-r[i])<1.e-6f;
    check(ms,"SIMD four-sample M/S and scalar remainder / M-M and S-minus-S reconstruction");
    std::complex<float>a[7],b[7],c[7]{};for(int i=0;i<7;++i){a[i]={float(i),-.3f};b[i]={.7f,float(i)*.1f};}complexMac4(c,a,b,.3f,7);bool simd=true;
    for(int i=0;i<7;++i)simd &= std::abs(c[i]-a[i]*b[i]*.3f)<1.e-6f;
    check(simd,"SIMD four-complex spectral MAC equals scalar including remainder");
#if defined(__SSE__) || defined(_M_X64)
    auto before=_mm_getcsr();bool ftz=false;{NumericEnvironment scope;ftz=(_mm_getcsr()&0x8040u)==0x8040u;}
    check(ftz && _mm_getcsr()==before,"MXCSR saves/restores exact environment and enables DAZ/FTZ");
#endif
    auto evidence=juce::JSON::parse(juce::File(__FILE__).getParentDirectory().getParentDirectory().getChildFile("docs/bode-original-coefficients.json"));
    auto *coefficients=evidence["coefficients"].getArray();BodePhasePair recovered;bool exact=coefficients && coefficients->size()==37;
    for(int i=0;exact && i<37;++i){uint64_t bits=0;std::memcpy(&bits,&recovered.coefficient[size_t(i)],8);exact &= bits==uint64_t((*coefficients)[i]["float64Bits"].toString().getHexValue64());}
    for(double hz:{100.,1000.,5000.,10000.}){auto z=std::exp(std::complex<double>(0,-2*3.14159265358979323846*hz/48000));std::complex<double>a=1,b=-(recovered.coefficient[36]+z)/(1.+recovered.coefficient[36]*z);
        for(int i=0;i<18;++i)a*=(recovered.coefficient[size_t(i)]+z)/(1.+recovered.coefficient[size_t(i)]*z);
        for(int i=18;i<36;++i)b*=(recovered.coefficient[size_t(i)]+z)/(1.+recovered.coefficient[size_t(i)]*z);
        exact &= std::abs(std::arg(b/a)-3.14159265358979323846*.5)<1.e-4;}
    check(exact,"Recovered Bode coefficients match all 37 IEEE-754 words / quadrature phase at 48kHz");
    auto foreign=juce::JSON::parse(juce::File(__FILE__).getParentDirectory().getParentDirectory().getChildFile("docs/runtime-host-parameters.json"));
    auto *records=foreign["parameters"].getArray();std::set<juce::int64> observed;int midiCount=0,overlap=0;
    auto domains=juce::JSON::parse(juce::File(__FILE__).getParentDirectory().getParentDirectory().getChildFile("docs/parameter-domains.json"));
    if(records)for(auto &record:*records){auto id=juce::int64(record["id"]);observed.insert(id);midiCount+=id>=500000000 && id<516000000;}
    check(records && records->size()==2623 && observed.size()==2623 && midiCount==2080 && int(domains["numericIntersectionCount"])==420,"Runtime host catalog: 2623 distinct IDs, 2080 MIDI, 420 numeric overlaps; static domain separate");
    juce::ValueTree tree("AuroraState");tree.setProperty("vendorUnknown","preserve",nullptr);
    auto *metadata=new juce::DynamicObject;metadata->setProperty("name","Original test");metadata->setProperty("unknown",123);
    auto *state=new juce::DynamicObject;state->setProperty("schema",2);state->setProperty("futureField","retained");state->setProperty("tree",treeToJson(tree));
    PresetDocument doc{juce::var(metadata),juce::var(state)},decoded;juce::MemoryBlock packed,again;
    bool stateOK=encodeDocument(doc,packed)&&decodeDocument(packed.getData(),int(packed.getSize()),decoded)&&encodeDocument(decoded,again)&&packed==again;
    stateOK &= int(decoded.metadata["unknown"])==123 && decoded.state["futureField"].toString()=="retained";
    auto truncated=packed;truncated.setSize(truncated.getSize()-1);PresetDocument rejected;
    stateOK &= !decodeDocument(truncated.getData(),int(truncated.getSize()),rejected);
    state->setProperty("schema",1);stateOK &= encodeDocument(doc,packed)&&decodeDocument(packed.getData(),int(packed.getSize()),decoded)&&int(decoded.state["schema"])==2;
    state->setProperty("schema",999);stateOK &= encodeDocument(doc,packed)&&!decodeDocument(packed.getData(),int(packed.getSize()),decoded);
    check(stateOK,"AuroraState JSON+CBOR+Zstd / metadata+unknown fields / lossless document roundtrip / migration / future-version rejection");
    auto catalogFile=juce::File(__FILE__).getParentDirectory().getParentDirectory().getChildFile("docs/canonical-parameters.json");
    auto catalog=juce::JSON::parse(catalogFile);auto *parameters=catalog["parameters"].getArray();std::set<juce::int64> ids;int known=0;
    if(parameters)for(auto &item:*parameters){ids.insert(juce::int64(item["canonicalId"]));known+=item["descriptorStatus"].toString()=="static-confirmed";}
    check(parameters && parameters->size()==2637 && ids.size()==2637 && known==35,"Static catalog: 2637 distinct canonical IDs; 35 confirmed RoutingSlot descriptors; unknown descriptors explicit");
}
