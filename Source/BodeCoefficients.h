#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace aurora {
// User-provided runtime capture, corroborated constructor 0x1803573f0.
// Fixed series observed at 48kHz; no claim of all-rate source equivalence.
inline constexpr std::array<uint64_t,37> bodeCoefficientBits{
    0xbfef25194e8e3454ULL,
    0xbfee88eff648c52aULL,
    0xbfef80c3e37dd1f0ULL,
    0xbfed81bbc57faeb1ULL,
    0xbfefb63d714beab6ULL,
    0xbfebcdf3584e7d37ULL,
    0xbfefd5556cda81c1ULL,
    0xbfe910ded5cbdaf1ULL,
    0xbfefe7664b829bf5ULL,
    0xbfe4d9911b1eb38aULL,
    0xbfeff1ed3d454f30ULL,
    0xbfdd7f2fa82199dbULL,
    0xbfeff821d4853172ULL,
    0xbfca2f7c179a41b0ULL,
    0xbfeffbeb2f767ff9ULL,
    0x3fbec64ed2d77ab8ULL,
    0xbfeffe7325039f53ULL,
    0x3fe14beb0c8d7203ULL,
    0xbfef590b83c0c67dULL,
    0xbfee16325444f8d5ULL,
    0xbfef9f190f4cdcc7ULL,
    0xbfecc2920ca9402bULL,
    0xbfefc7e2884b2d5aULL,
    0xbfea975e6dee308bULL,
    0xbfefdf9581685927ULL,
    0xbfe72bba3fa17b85ULL,
    0xbfefed5d620695c5ULL,
    0xbfe20de16c014672ULL,
    0xbfeff56d94811d09ULL,
    0xbfd5d3f70e8ea862ULL,
    0xbfeffa3e1f837f10ULL,
    0xbfaa3bbf08863380ULL,
    0xbfeffd4961d54b64ULL,
    0x3fd418441d64adb5ULL,
    0xbfefff7ef502edf3ULL,
    0x3fea57912b869157ULL,
    0xbfeee147ae147ae1ULL
};
inline std::array<double,37> recoveredBodeCoefficients(){std::array<double,37> result{};for(size_t i=0;i<37;++i)std::memcpy(&result[i],&bodeCoefficientBits[i],8);return result;}
}
