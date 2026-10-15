#pragma once
#include "../third_party/zstd/lib/zstd.h"
#include <JuceHeader.h>
#include "Cbor.h"
namespace aurora {
// Independent preset container, intentionally distinct from XferJson.
// Header: AuroraJson\0, u32 metadata bytes, JSON, u32 CBOR bytes, u8 selector=2,
// u32 compressed bytes, Zstandard(CBOR {"state": byte-string JUCE XML binary}).
inline bool encodeLegacyPreset(const juce::MemoryBlock &xml, juce::MemoryBlock &result) {
    if (xml.getSize() > 128 * 1024 * 1024)
        return false;
    juce::MemoryOutputStream cbor;
    cbor.writeByte(char(0xa1));
    cbor.writeByte(char(0x65));
    cbor.write("state", 5);
    cbor.writeByte(char(0x5a));
    cbor.writeIntBigEndian(int(xml.getSize()));
    cbor.write(xml.getData(), xml.getSize());
    juce::MemoryBlock compressed(ZSTD_compressBound(cbor.getDataSize()));
    size_t size = ZSTD_compress(compressed.getData(), compressed.getSize(), cbor.getData(),
                                cbor.getDataSize(), 3);
    if (ZSTD_isError(size))
        return false;
    juce::MemoryOutputStream out;
    out.write("AuroraJson", 11);
    juce::String metadata = "{\"product\":\"Aurora\",\"version\":\"0.1.2\",\"schema\":1,"
                            "\"payload\":\"CBOR+Zstandard\"}";
    out.writeInt(metadata.getNumBytesAsUTF8());
    out.write(metadata.toRawUTF8(), size_t(metadata.getNumBytesAsUTF8()));
    out.writeInt(int(cbor.getDataSize()));
    out.writeByte(2);
    out.writeInt(int(size));
    out.write(compressed.getData(), size);
    result.replaceAll(out.getData(), out.getDataSize());
    return true;
}
inline bool decodeLegacyPreset(const void *data, int size, juce::MemoryBlock &xml) {
    if (size < 11 || std::memcmp(data, "AuroraJson", 11) != 0) {
        xml.replaceAll(data, size_t(std::max(0, size)));
        return size > 0;
    }
    juce::MemoryInputStream in(data, size_t(size), false);
    in.skipNextBytes(11);
    int meta = in.readInt();
    if (meta < 0 || meta > 65536 || in.getNumBytesRemaining() < meta + 9)
        return false;
    in.skipNextBytes(meta);
    int plain = in.readInt();
    int selector = static_cast<unsigned char>(in.readByte());
    int compressed = in.readInt();
    if (selector != 2 || plain < 12 || plain > 128 * 1024 * 1024 || compressed <= 0 ||
        compressed > in.getNumBytesRemaining())
        return false;
    juce::MemoryBlock packed;
    in.readIntoMemoryBlock(packed, size_t(compressed));
    juce::MemoryBlock payload{size_t(plain)};
    size_t length =
        ZSTD_decompress(payload.getData(), payload.getSize(), packed.getData(), packed.getSize());
    if (ZSTD_isError(length) || length != size_t(plain))
        return false;
    juce::MemoryInputStream cbor(payload, false);
    if (static_cast<unsigned char>(cbor.readByte()) != 0xa1 ||
        static_cast<unsigned char>(cbor.readByte()) != 0x65)
        return false;
    char key[5];
    cbor.read(key, 5);
    if (std::memcmp(key, "state", 5) || static_cast<unsigned char>(cbor.readByte()) != 0x5a)
        return false;
    int bytes = cbor.readIntBigEndian();
    if (bytes <= 0 || bytes != cbor.getNumBytesRemaining())
        return false;
    xml.reset();
    cbor.readIntoMemoryBlock(xml, size_t(bytes));
    return true;
}

struct PresetDocument { juce::var metadata, state; };
inline void cborHead(juce::MemoryOutputStream &out, int major, uint64_t n) {
    if (n<24) out.writeByte(char((major<<5)|int(n)));
    else if (n<=255) { out.writeByte(char((major<<5)|24)); out.writeByte(char(n)); }
    else if (n<=65535) { out.writeByte(char((major<<5)|25)); out.writeShortBigEndian(short(n)); }
    else if (n<=0xffffffffu) { out.writeByte(char((major<<5)|26)); out.writeIntBigEndian(int(n)); }
    else { out.writeByte(char((major<<5)|27)); out.writeInt64BigEndian(juce::int64(n)); }
}
inline bool cborWrite(juce::MemoryOutputStream &out, const juce::var &v, int depth=0) {
    if(depth>64 || out.getDataSize()>128*1024*1024) return false;
    if(v.isVoid() || v.isUndefined()) out.writeByte(char(0xf6));
    else if(v.isBool()) out.writeByte(char(bool(v)?0xf5:0xf4));
    else if(v.isInt() || v.isInt64()) { auto i=juce::int64(v); cborHead(out,i<0?1:0,i<0?uint64_t(-(i+1)):uint64_t(i)); }
    else if(v.isDouble()) { double x=double(v); if(!std::isfinite(x))return false; uint64_t bits; std::memcpy(&bits,&x,8); out.writeByte(char(0xfb)); out.writeInt64BigEndian(juce::int64(bits)); }
    else if(auto *binary=v.getBinaryData()) { cborHead(out,2,binary->getSize());out.write(binary->getData(),binary->getSize()); }
    else if(v.isString()) { auto text=v.toString(); cborHead(out,3,size_t(text.getNumBytesAsUTF8())); out.write(text.toRawUTF8(),size_t(text.getNumBytesAsUTF8())); }
    else if(auto *a=v.getArray()) { cborHead(out,4,size_t(a->size())); for(auto &item:*a) if(!cborWrite(out,item,depth+1))return false; }
    else if(auto *o=v.getDynamicObject()) { auto &props=o->getProperties(); cborHead(out,5,size_t(props.size())); for(int i=0;i<props.size();++i) { if(!cborWrite(out,props.getName(i).toString(),depth+1)||!cborWrite(out,props.getValueAt(i),depth+1))return false; } }
    else return false;
    return true;
}
inline juce::var treeToJson(const juce::ValueTree &tree) {
    auto *root=new juce::DynamicObject, *props=new juce::DynamicObject;
    root->setProperty("type",tree.getType().toString());
    for(int i=0;i<tree.getNumProperties();++i) props->setProperty(tree.getPropertyName(i),tree.getProperty(tree.getPropertyName(i)));
    root->setProperty("properties",juce::var(props));
    juce::Array<juce::var> children; for(auto child:tree) children.add(treeToJson(child));
    root->setProperty("children",children); return juce::var(root);
}
inline juce::ValueTree jsonToTree(const juce::var &v, int depth=0) {
    if(depth>64 || !v.isObject() || !v["type"].isString() || v["type"].toString().isEmpty()) return {};
    juce::ValueTree tree(v["type"].toString());
    auto *props=v["properties"].getDynamicObject(); auto *children=v["children"].getArray();
    if(!props || !children)return {};
    for(auto &item:props->getProperties()) tree.setProperty(item.name,item.value,nullptr);
    for(auto &child:*children) { auto node=jsonToTree(child,depth+1); if(!node.isValid())return {}; tree.addChild(node,-1,nullptr); }
    return tree;
}
inline bool encodeDocument(const PresetDocument &doc, juce::MemoryBlock &result) {
    if(!doc.metadata.isObject()||!doc.state.isObject())return false;
    auto metadata=juce::JSON::toString(doc.metadata,true);
    if(metadata.getNumBytesAsUTF8()>8*1024*1024)return false;
    juce::MemoryOutputStream payload; if(!cborWrite(payload,doc.state))return false;
    juce::MemoryBlock packed(ZSTD_compressBound(payload.getDataSize()));
    auto bytes=ZSTD_compress(packed.getData(),packed.getSize(),payload.getData(),payload.getDataSize(),3);
    if(ZSTD_isError(bytes))return false;
    juce::MemoryOutputStream out;
    out.write("AuroraState",12); out.writeInt64(metadata.getNumBytesAsUTF8());
    out.write(metadata.toRawUTF8(),size_t(metadata.getNumBytesAsUTF8()));
    out.writeInt(int(payload.getDataSize())); out.writeInt(2); out.write(packed.getData(),bytes);
    result.replaceAll(out.getData(),out.getDataSize());return true;
}
inline bool decodeDocument(const void *data,int size,PresetDocument &doc) {
    if(size<28 || size>128*1024*1024 || std::memcmp(data,"AuroraState",12))return false;
    juce::MemoryInputStream in(data,size_t(size),false); in.skipNextBytes(12);
    auto meta=in.readInt64();
    if(meta<2 || meta>8*1024*1024 || meta>in.getNumBytesRemaining()-8)return false;
    juce::MemoryBlock raw;in.readIntoMemoryBlock(raw,size_t(meta));
    juce::var m; if(juce::JSON::parse(juce::String::fromUTF8((const char*)raw.getData(),int(raw.getSize())),m).failed()||!m.isObject())return false;
    int plain=in.readInt(),selector=in.readInt(); auto packed=in.getNumBytesRemaining();
    if(selector!=2 || plain<=0 || plain>128*1024*1024 || packed<=0)return false;
    auto *frame=static_cast<const uint8_t*>(data)+in.getPosition();
    auto frameSize=ZSTD_findFrameCompressedSize(frame,size_t(packed));
    if(ZSTD_isError(frameSize)||frameSize!=size_t(packed))return false;
    juce::MemoryBlock bytes{size_t(plain)};
    auto length=ZSTD_decompress(bytes.getData(),bytes.getSize(),frame,size_t(packed));
    if(ZSTD_isError(length)||length!=size_t(plain))return false;
    juce::var state; if(!SerumCbor(bytes).read(state)||!state.isObject())return false;
    int version=int(state.getProperty("schema",0));
    if(version<1||version>2)return false;
    // Schema 1 documents used the same structured tree; migration is additive.
    state.getDynamicObject()->setProperty("schema",2);
    if(!jsonToTree(state["tree"]).isValid())return false;
    doc={m,state};return true;
}
inline bool encodePreset(const juce::MemoryBlock &xml,juce::MemoryBlock &result,
                         juce::var metadata={},juce::var extra={}) {
    auto source=juce::AudioProcessor::getXmlFromBinary(xml.getData(),int(xml.getSize()));
    if(!source)return false;
    if(!metadata.isObject()) { auto *o=new juce::DynamicObject; o->setProperty("product","Aurora");o->setProperty("version","0.2.0");metadata=juce::var(o); }
    if(!extra.isObject())extra=juce::var(new juce::DynamicObject);
    extra=extra.clone();extra.getDynamicObject()->setProperty("schema",2);
    extra.getDynamicObject()->setProperty("tree",treeToJson(juce::ValueTree::fromXml(*source)));
    return encodeDocument({metadata,extra},result);
}
inline bool decodePreset(const void *data,int size,juce::MemoryBlock &xml,PresetDocument *document=nullptr) {
    if(size>=12 && !std::memcmp(data,"AuroraState",12)) {
        PresetDocument doc;if(!decodeDocument(data,size,doc))return false;
        auto tree=jsonToTree(doc.state["tree"]);auto source=tree.createXml(); if(!source)return false;
        juce::AudioProcessor::copyXmlToBinary(*source,xml);if(document)*document=doc;return true;
    }
    if(document)*document={};
    return decodeLegacyPreset(data,size,xml);
}
} // namespace aurora
