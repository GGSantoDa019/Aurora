#pragma once
#include "WavetableWarpNames.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
namespace aurora {
inline float wtRound(float x){volatile float value=x;return value;}
struct PhaseRandom {
    std::array<uint64_t,624> state{};unsigned index=312;
    explicit PhaseRandom(uint64_t seed=5489){reset(seed);}
    void reset(uint64_t seed){state={};state[0]=seed;index=312;
        for(unsigned i=1;i<312;++i)state[i]=6364136223846793005ull*(state[i-1]^(state[i-1]>>62))+i;}
    void twist(unsigned source,unsigned destination){for(unsigned i=0;i<312;++i){
        uint64_t a=state[source+i],b=i<311?state[source+i+1]:state[destination];
        uint64_t mid=i<156?state[source+i+156]:state[destination+i-156];
        state[destination+i]=mid^(((a&0xffffffff80000000ull)|(b&0x7fffffffull))>>1)^((b&1)?0xb5026f5aa96619e9ull:0);
    }}
    uint64_t next(){if(index==312)twist(0,312);else if(index>623){twist(312,0);index=0;}
        uint64_t x=state[index++];x^=(x>>29)&0x5555555555555555ull;
        x^=(x&0x38eb3ffff6d3ull)<<17;x^=(x&0x7ffbf77ull)<<37;return x^(x>>43);}
    static float uniform(uint64_t value){if(!value)return 0;unsigned bits=0;for(auto v=value;v;v>>=1)++bits;
        unsigned shift=bits>24?bits-24:0;uint64_t q=value>>shift;
        if(shift){uint64_t r=value&((1ull<<shift)-1),half=1ull<<(shift-1);if(r>half || (r==half && (q&1)))++q;}
        return std::ldexp(float(q),int(shift)-64);}
    float nextUniform(){return uniform(next());}
    double initialPhase(double base,double amount,float caller,unsigned member,double explicitPhase,bool reuse,int64_t stored){
        float random=member?nextUniform():caller;
        double p=explicitPhase>=0?explicitPhase+(member?double(random)*amount*amount:0):reuse?std::ldexp(double(stored),-43):double(random)*amount*amount+base;
        return p-std::floor(p);
    }
    static double phase(double base,double amount,float random){double p=base+double(random)*amount*amount;return p-std::floor(p);}
};
inline double wavetableExp(double x){
    if(x>708.3964185322641)return 1.7976931348623157e308;if(x< -708.3964185322641)return 0;
    int64_t n=int64_t(std::floor(x*1.4426950408889634+.5));
    double t=(n*-1.4286068203094173e-06)+x;t=(n*-.693145751953125)+t;
    double q=t*t,a=((q*.00012617719307481058+.030299440770744195)*q+1)*t;
    double b=(((q*3.0019850513866446e-06+.002524483403496841)*q+.22726554820815503)*q+2)-a;
    a/=b;uint64_t bits=uint64_t(n)*0x10000000000000ull+0x3ff0000000000000ull;double scale;std::memcpy(&scale,&bits,8);
    return scale*((a+1)+a);
}
inline std::array<float,32> wavetablePan(int n,float blend,float width){
    std::array<float,32> out{};n=std::clamp(n,1,16);float a=wtRound(1-width),k=1.4158915281295776f;
    if(n==1){out[0]=out[1]=1;return out;}
    if(n==2){float l=wtRound(wtRound(wtRound(k+a)*.7079460024833679f)*a),r=wtRound(wtRound(wtRound(k+width)*.7079460024833679f)*width);out[0]=out[3]=l;out[1]=out[2]=r;return out;}
    float q=wtRound(blend*blend),side=float(((-.73764*double(q))+1.2841)*double(q)+.04437199980020523);
    float center=wtRound(wtRound(q*-.553659975528717f)+.9978500008583069f);
    float l=wtRound(wtRound(k+a)*a),r=wtRound(wtRound(k+width)*width);
    float left=wtRound(l*side),right=wtRound(r*side);int middle=n/2;
    float cl=wtRound(((n&2)?r:l)*center),cr=wtRound(((n&2)?l:r)*center);
    for(int i=0;i<n;++i){if((n&1) && i==middle){out[size_t(i*2)]=out[size_t(i*2+1)]=wtRound(center*.9579457640647888f);continue;}
        if(!(n&1) && i==middle){out[size_t(i*2)]=cl;out[size_t(i*2+1)]=cr;}
        else if(!(n&1) && i==middle-1){out[size_t(i*2)]=cr;out[size_t(i*2+1)]=cl;}
        else{out[size_t(i*2)]=left;out[size_t(i*2+1)]=right;}
        std::swap(left,right);}
    return out;
}
inline std::array<float,2> wavetableGlobalPan(float pan,float normalization){
    double c=(double(wtRound(1-pan))*.4158915687682758)*double(pan);
    return {float(((1+c)-pan)*normalization),float((c+pan)*normalization)};
}
inline std::array<double,16> wavetableDetune(int n,float amount,float range,int stack,int shape,float shift,const std::array<double,16>&random){
    std::array<double,16> out{};double depth=std::pow(double(amount),2.5)*range/12.;int middle=n/2;
    for(int i=0;i<n;++i){double x=n==1?0:double(i)/(n-1)+double(i)/(n-1)-1;
        if(x!=0){if(shape==1)x=std::tan(x)*.64209261593;else if(shape==2)x=std::abs(x)*x;
            else if(shape==3)x=std::sin(x*1.5707963267948966);else if(shape==4)x=random[size_t(i)]*(x<0?-1:1);}
        x+=shift;if(x>1)x=2-x;if(x< -1)x=-2-x;x*=depth;
        bool center=i==middle || (!(n&1) && i==middle-1);
        if(stack>=7){if(center)x-=stack-6;}else if(stack>0 && !center){int d=std::abs(i-middle);x+=stack<4?double(d%(stack+1)):double(d%(stack-3))+(d%4==2?.5833333333333334:0);}
        out[size_t(i)]=wavetableExp(x*.6931471805599453);
    }return out;
}
inline float wavetableAmount(float a,int mode){a=std::clamp(a,0.f,1.f);
    if(mode==2 || mode==6)return wtRound(wtRound(a*-.4000000059604645f)+.5f);
    if(mode==3 || mode==7)return wtRound(wtRound(a*.4000000059604645f)+.5f);
    if(mode==4 || mode==8)return wtRound(wtRound(a*.800000011920929f)+.10000000149011612f);
    if(mode==5)return wtRound(1-a);if(mode==9)return wtRound(a+a);
    if(mode==10)return wtRound(wtRound(a*.8999999761581421f)+.05000000074505806f);
    if(mode==15){a=wtRound(1-a);float sq=wtRound(a*a);return wtRound(wtRound(sq*sq)*a);}
    if(mode==23)return float(double(a)*.95+.05);
    if(mode>=33 && mode<=56){float sq=wtRound(a*a);return wtRound(sq*sq);}
    if(mode==57)return float(.6*double(a)*a);return a;
}
inline bool supportedWavetableWarp(int mode){
    if(mode>=0 && (mode<=8 || mode==10))return true;
    if(mode>=33 && mode<=56)return ((mode-33)%6)<4;
    if(mode>=58 && mode<=69)return ((mode-58)%6)<4;
    return false;
}
inline double wavetablePhase(double x,float a,int mode){
    if(mode>=2 && mode<=4){if(x==0 || x==.5)return x;double b=wtRound(wtRound(a+a)-1);
        if(x<.5)return x*a/((b*2)*x-a+1);double t=x+x-1;return ((double(wtRound(wtRound(3*a)-2))*t-a)*.5)/(b*t-a);}
    if(mode==5)return x<a?x/a:0;
    if(mode>=6 && mode<=8)return x<a?(x*.5)/a:((a-x)/double(wtRound(a-1))+1)*.5;
    if(mode==10){double v=x<=.5?x:1-x;return a*.5<=v?((a-(v+v))/double(wtRound(a-1))+1)*.5:v/a;}
    return x;
}
struct ExternalWarp {
    std::array<float,4> value{0,0,0,1};
    void apply(int slot,int type,float amount,float sample,bool noise=false){
        float source=noise?wtRound(wtRound(.5f*amount)*sample):sample;
        source=wtRound(source+(type==4?-1.f:0.f));float prior=slot?value[3]:1;
        if(type==1)value[0]=wtRound(value[0]+wtRound(amount*source));
        else if(type==2)value[0]=float(double(value[0])-1+wavetableExp(wtRound(amount*source)));
        else if(type==3)value[1]=wtRound(value[1]+wtRound(amount*source));
        else if(type==4){float scale=wtRound(8*amount);value[2]=wtRound(value[2]+wtRound(wtRound(scale*source)+scale));}
        else if(type==5){float scale=wtRound(wtRound(2*prior)*amount);value[3]=wtRound(value[3]+wtRound(scale*source));}
        else if(type==6){float scale=wtRound(wtRound(2*prior)*amount),bias=wtRound(-prior*amount);value[3]=wtRound(value[3]+wtRound(wtRound(scale*source)+bias));}
    }
};
}
