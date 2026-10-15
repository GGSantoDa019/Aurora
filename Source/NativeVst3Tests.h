#pragma once
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstnoteexpression.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
namespace aurora_test {
using namespace Steinberg;
using namespace Steinberg::Vst;
struct Queue final : IParamValueQueue {
    ParamID id=0;
    tresult PLUGIN_API queryInterface(const TUID,void **obj) override{*obj=nullptr;return kNoInterface;}
    uint32 PLUGIN_API addRef() override{return 1;}
    uint32 PLUGIN_API release() override{return 1;}
    ParamID PLUGIN_API getParameterId() override{return id;}
    int32 PLUGIN_API getPointCount() override{return 2;}
    tresult PLUGIN_API getPoint(int32 i,int32&offset,ParamValue&value) override{if(i<0||i>1)return kResultFalse;offset=i?900:73;value=i?.8:0;return kResultOk;}
    tresult PLUGIN_API addPoint(int32,ParamValue,int32&) override{return kNotImplemented;}
};
struct Changes final : IParameterChanges {
    Queue queue;
    tresult PLUGIN_API queryInterface(const TUID,void **obj) override{*obj=nullptr;return kNoInterface;}
    uint32 PLUGIN_API addRef() override{return 1;}
    uint32 PLUGIN_API release() override{return 1;}
    int32 PLUGIN_API getParameterCount() override{return 1;}
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override{return i?nullptr:&queue;}
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&,int32&) override{return nullptr;}
};
inline bool sampleAccurateHost(juce::AudioPluginInstance &instance){
    struct Visitor : juce::ExtensionsVisitor {
        IComponent *component=nullptr;
        void visitVST3Client(const VST3Client &client) override{component=client.getIComponentPtr();}
    }visitor;
    instance.getExtensions(visitor);if(!visitor.component)return false;
    IAudioProcessor *processor=nullptr;
    if(visitor.component->queryInterface(IAudioProcessor::iid,(void**)&processor)!=kResultOk || !processor)return false;
    Changes changes;bool found=false;
    for(int i=0;i<instance.getParameters().size();++i)if(auto *p=instance.getHostedParameter(i);p && p->getName(128)=="Master"){
        changes.queue.id=ParamID(p->getParameterID().getLargeIntValue());found=true;break;}
    float l[1024]{},r[1024]{};float *channels[]{l,r};AudioBusBuffers bus{};bus.numChannels=2;bus.channelBuffers32=channels;
    ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=1024;data.numOutputs=1;data.outputs=&bus;data.inputParameterChanges=&changes;
    std::cout << "INFO native queue test parameter=" << changes.queue.id << " found=" << found << "\n";
    bool ok=found && processor->process(data)==kResultOk;processor->release();
    std::cout << "INFO native queue processing complete\n";
    float before=0,middle=0,after=0;
    for(int i=0;i<73;++i)before=std::max(before,std::abs(l[i]));
    for(int i=600;i<890;++i)middle=std::max(middle,std::abs(l[i]));
    for(int i=980;i<1024;++i)after=std::max(after,std::abs(l[i]));
    return ok && before>1.e-5f && middle<1.e-7f && after>1.e-5f;
}
inline bool expressionAnnouncement(juce::AudioPluginInstance &instance){
    struct Visitor : juce::ExtensionsVisitor {IEditController *controller=nullptr;void visitVST3Client(const VST3Client&client) override{controller=client.getIEditControllerPtr();}}visitor;
    instance.getExtensions(visitor);if(!visitor.controller)return false;
    INoteExpressionController *expressions=nullptr;
    if(visitor.controller->queryInterface(INoteExpressionController::iid,(void**)&expressions)!=kResultOk || !expressions)return false;
    juce::AudioProcessorParameter *mpe=nullptr;for(auto *p:instance.getParameters())if(p->getName(128)=="MPE enabled")mpe=p;
    if(!mpe){expressions->release();return false;}
    auto update=[&](float v){mpe->setValueNotifyingHost(v);juce::AudioBuffer<float> audio(2,256);audio.clear();juce::MidiBuffer events;instance.processBlock(audio,events);};
    update(0);
    bool okay=expressions->getNoteExpressionCount(0,0)==4 && expressions->getNoteExpressionCount(0,1)==0 && expressions->getNoteExpressionCount(1,0)==0;
    int ids[]{0,1,2,5};for(int i=0;i<4;++i){NoteExpressionTypeInfo info{};okay &= expressions->getNoteExpressionInfo(0,0,i,info)==kResultOk && info.typeId==unsigned(ids[i]);}
    String128 text{};NoteExpressionValue value=0;
    okay &= expressions->getNoteExpressionStringByValue(0,0,2,.55,text)==kResultOk && expressions->getNoteExpressionValueByString(0,0,2,text,value)==kResultOk && std::abs(value-.55)<1.e-6;
    update(1);okay &= expressions->getNoteExpressionCount(0,0)==0;
    expressions->release();return okay;
}

}

