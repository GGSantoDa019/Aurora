#pragma once
#include <JuceHeader.h>
#include <limits>
#include <cmath>
namespace aurora {
class SerumCbor {
    const uint8_t *cursor;
    const uint8_t *end;
    int items = 0;
    struct Invalid {};
    const uint8_t *take(uint64_t n) {
        if (n > uint64_t(end - cursor))
            throw Invalid{};
        auto *p = cursor;
        cursor += size_t(n);
        return p;
    }
    uint64_t integer(int ai) {
        if (ai < 24)
            return uint64_t(ai);
        int bytes = ai == 24 ? 1 : ai == 25 ? 2 : ai == 26 ? 4 : ai == 27 ? 8 : 0;
        if (!bytes)
            throw Invalid{};
        auto *p = take(uint64_t(bytes));
        uint64_t value = 0;
        for (int i = 0; i < bytes; ++i)
            value = (value << 8) | p[i];
        return value;
    }
    juce::var value(int depth) {
        if (depth > 64 || ++items > 2000000)
            throw Invalid{};
        int token = *take(1), major = token >> 5, ai = token & 31;
        if (major == 7) {
            if (ai == 20 || ai == 21)
                return ai == 21;
            if (ai == 22 || ai == 23)
                return {};
            double result = 0;
            if (ai == 25) {
                auto bits = integer(25);
                int exponent = int((bits >> 10) & 31), fraction = int(bits & 1023);
                if (exponent == 31)
                    throw Invalid{};
                result = exponent ? std::ldexp(1. + fraction / 1024., exponent - 15)
                                  : std::ldexp(double(fraction), -24);
                if (bits & 32768)
                    result = -result;
            } else if (ai == 26) {
                uint32_t bits = uint32_t(integer(26));
                float f;
                std::memcpy(&f, &bits, 4);
                result = f;
            } else if (ai == 27) {
                uint64_t bits = integer(27);
                std::memcpy(&result, &bits, 8);
            } else
                throw Invalid{};
            if (!std::isfinite(result))
                throw Invalid{};
            return result;
        }
        bool indefinite = ai == 31;
        uint64_t n = indefinite ? 0 : integer(ai);
        if (major <= 1) {
            if (indefinite || n > uint64_t(std::numeric_limits<juce::int64>::max()))
                throw Invalid{};
            return juce::var(major == 0 ? juce::int64(n) : -1 - juce::int64(n));
        }
        if (major == 2 || major == 3) {
            juce::MemoryBlock raw;
            if (!indefinite) {
                auto *p = take(n);
                raw.append(p, size_t(n));
            } else {
                while (cursor < end && *cursor != 255) {
                    int head = *take(1);
                    if ((head >> 5) != major || (head & 31) == 31)
                        throw Invalid{};
                    uint64_t length = integer(head & 31);
                    auto *p = take(length);
                    raw.append(p, size_t(length));
                }
                if (*take(1) != 255)
                    throw Invalid{};
            }
            if (major == 2)
                return juce::var(raw);
            auto *text = static_cast<const char *>(raw.getData());
            if (!juce::CharPointer_UTF8::isValidString(text, int(raw.getSize())))
                throw Invalid{};
            return juce::String::fromUTF8(text, int(raw.getSize()));
        }
        if (major == 4) {
            if (n > 2000000)
                throw Invalid{};
            juce::Array<juce::var> array;
            for (uint64_t i = 0; indefinite || i < n; ++i) {
                if (indefinite && cursor < end && *cursor == 255) {
                    ++cursor;
                    break;
                }
                array.add(value(depth + 1));
            }
            return array;
        }
        if (major == 5) {
            if (n > 2000000)
                throw Invalid{};
            auto object = std::make_unique<juce::DynamicObject>();
            for (uint64_t i = 0; indefinite || i < n; ++i) {
                if (indefinite && cursor < end && *cursor == 255) {
                    ++cursor;
                    break;
                }
                auto key = value(depth + 1);
                if (!key.isString() || key.toString().isEmpty() || key.toString().containsChar(0))
                    throw Invalid{};
                juce::Identifier name(key.toString());
                if (object->hasProperty(name))
                    throw Invalid{};
                object->setProperty(name, value(depth + 1));
            }
            return juce::var(object.release());
        }
        if (major == 6 && !indefinite)
            return value(depth + 1);
        throw Invalid{};
    }

  public:
    SerumCbor(const juce::MemoryBlock &bytes)
        : cursor(static_cast<const uint8_t *>(bytes.getData())), end(cursor + bytes.getSize()) {}
    bool read(juce::var &result) {
        try {
            result = value(0);
            return cursor == end;
        } catch (const Invalid &) {
            return false;
        }
    }
};

}
