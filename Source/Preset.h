#pragma once
#include "../third_party/zstd/lib/zstd.h"
#include <JuceHeader.h>
namespace aurora {
// Independent preset container, intentionally distinct from XferJson.
// Header: AuroraJson\0, u32 metadata bytes, JSON, u32 CBOR bytes, u8 selector=2,
// u32 compressed bytes, Zstandard(CBOR {"state": byte-string JUCE XML binary}).
inline bool encodePreset(const juce::MemoryBlock &xml, juce::MemoryBlock &result) {
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
    juce::String metadata = "{\"product\":\"Aurora\",\"version\":\"0.1.1\",\"schema\":1,"
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
inline bool decodePreset(const void *data, int size, juce::MemoryBlock &xml) {
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
} // namespace aurora
