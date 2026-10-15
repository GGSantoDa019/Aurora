#pragma once
#include "CoreInfrastructure.h"
#include <complex>
#include "BodeCoefficients.h"
namespace aurora {
struct Ring8192 {
    static constexpr unsigned mask=0x1fff;
    std::array<float,8192> data{}; unsigned cursor=0;
    void clear(){data.fill(0);cursor=0;}
    void push(float x){data[cursor]=x;cursor=(cursor+1)&mask;}
    float read(float delay) const {
        delay=juce::jlimit(1.f,8190.f,delay);
        float position=float(cursor)-delay;int i=int(std::floor(position));float f=position-std::floor(position);
        return data[unsigned(i)&mask]*(1-f)+data[unsigned(i+1)&mask]*f;
    }
};
struct CompressorDetector {
    float state=0;
    float tick(float x){return state+=(std::abs(x)-state)*.00025f;}
};
struct BodePhasePair {
    std::array<double,37> coefficient = recoveredBodeCoefficients();
    std::array<double,37> state{};
    static double section(double x,double a,double &s){double t=a*(x+s),y=s+t;s=x-t;return y;}
    std::array<double,2> tick(double x){
        double first=x,second=section(x,coefficient[36],state[36]);
        for(int j=0;j<18;++j)first=section(first,coefficient[size_t(j)],state[size_t(j)]);
        for(int j=18;j<36;++j)second=section(second,coefficient[size_t(j)],state[size_t(j)]);
        return {first,-second};
    }
    void reset(){state.fill(0);}
};
inline void complexMac4(std::complex<float>* dst,const std::complex<float>*a,const std::complex<float>*b,float gain,int n){
    int i=0;
#if defined(__SSE__) || defined(_M_X64)
    for(;i+4<=n;i+=4){
        auto ar=_mm_set_ps(a[i+3].real(),a[i+2].real(),a[i+1].real(),a[i].real());
        auto ai=_mm_set_ps(a[i+3].imag(),a[i+2].imag(),a[i+1].imag(),a[i].imag());
        auto br=_mm_set_ps(b[i+3].real(),b[i+2].real(),b[i+1].real(),b[i].real());
        auto bi=_mm_set_ps(b[i+3].imag(),b[i+2].imag(),b[i+1].imag(),b[i].imag());
        auto g=_mm_set1_ps(gain);float real[4],imag[4];
        _mm_storeu_ps(real,_mm_mul_ps(g,_mm_sub_ps(_mm_mul_ps(ar,br),_mm_mul_ps(ai,bi))));
        _mm_storeu_ps(imag,_mm_mul_ps(g,_mm_add_ps(_mm_mul_ps(ar,bi),_mm_mul_ps(ai,br))));
        for(int j=0;j<4;++j)dst[i+j]+=std::complex<float>(real[j],imag[j]);
    }
#endif
    for(;i<n;++i)dst[i]+=a[i]*b[i]*gain;
}
// Zero-latency direct head plus uniformly partitioned overlap-add tail.
// B=128, Nfft=2B. JUCE's inverse is normalized; compensate its 1/N after
// applying the explicitly stored 1/N spectral-product gain (unscaled-IFFT convention).
struct PartitionedFIR {
    static constexpr int B=128,N=2*B;
    float productGain=1.f/N;
    juce::dsp::FFT fft{8};
    std::array<float,B> head{},history{},input{},tail{},overlap{};
    std::array<std::complex<float>,N> time{},spectrum{},sum{},inverse{};
    std::vector<std::array<std::complex<float>,N>> kernels, spectra;
    int position=0,headCursor=0,blockCursor=0;
    PartitionedFIR(){head[0]=1;}
    void configure(const std::vector<float>&ir){
        head.fill(0);history.fill(0);input.fill(0);tail.fill(0);overlap.fill(0);
        position=headCursor=blockCursor=0;
        for(size_t i=0;i<std::min(ir.size(),size_t(B));++i)head[i]=ir[i];
        int partitions=int((ir.size()>B?ir.size()-B:0)+B-1)/B;
        kernels.resize(size_t(partitions));spectra.resize(size_t(partitions));
        for(auto &s:spectra)s.fill({});
        for(int p=0;p<partitions;++p){
            time.fill({}); // Every IR partition is padded B -> 2B.
            for(int j=0;j<B;++j){size_t k=size_t(B+p*B+j);if(k<ir.size())time[size_t(j)]=ir[k];}
            fft.perform(time.data(),kernels[size_t(p)].data(),false);
        }
    }
    float tick(float x){
        history[size_t(headCursor)]=x;float y=0;
        for(int j=0;j<B;++j)y+=head[size_t(j)]*history[size_t((headCursor-j+B)%B)];
        headCursor=(headCursor+1)%B;y+=tail[size_t(position)];input[size_t(position)]=x;
        if(++position==B){
            position=0;
            if(!kernels.empty()){
                time.fill({});for(int j=0;j<B;++j)time[size_t(j)]=input[size_t(j)];
                fft.perform(time.data(),spectra[size_t(blockCursor)].data(),false);sum.fill({});
                int count=int(kernels.size());
                for(int p=0;p<count;++p)complexMac4(sum.data(),spectra[size_t((blockCursor-p+count)%count)].data(),kernels[size_t(p)].data(),productGain,N);
                fft.perform(sum.data(),inverse.data(),true);
                for(int j=0;j<B;++j){tail[size_t(j)]=inverse[size_t(j)].real()*N+overlap[size_t(j)];overlap[size_t(j)]=inverse[size_t(j+B)].real()*N;}
                blockCursor=(blockCursor+1)%count;
            }
        }
        return y;
    }
};
struct StereoFIR {
    std::array<PartitionedFIR,2> channels;
    double sampleRate=48000;
    void prepare(double sr){sampleRate=sr;for(auto &c:channels)c.configure({1});}
    void load(const juce::AudioBuffer<float>&ir,double irRate){
        int size=std::max(1,int(std::ceil(ir.getNumSamples()*sampleRate/irRate)));
        for(int c=0;c<2;++c){std::vector<float> data(size_t(size),0.f);
            for(int i=0;i<size;++i){double source=i*irRate/sampleRate;int a=std::min(ir.getNumSamples()-1,int(source)),b=std::min(ir.getNumSamples()-1,a+1);float f=float(source-a);
                data[size_t(i)]=ir.getSample(std::min(c,ir.getNumChannels()-1),a)*(1-f)+ir.getSample(std::min(c,ir.getNumChannels()-1),b)*f;}
            channels[size_t(c)].configure(data);
        }
    }
    void process(float*l,float*r,int n){for(int i=0;i<n;++i){l[i]=channels[0].tick(l[i]);r[i]=channels[1].tick(r[i]);}}
};
}
