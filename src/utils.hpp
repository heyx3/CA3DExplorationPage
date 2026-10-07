#pragma once

#include <cstdint>
#include <array>
#include <utility>
#include <cassert>
#include <type_traits>
#include <random>
#include <limits>

#include <pcg_random.hpp>
#include <xtensor/containers/xtensor.hpp>
#include <xtensor/views/xview.hpp>
#include <xtensor/core/xvectorize.hpp>
#include <xtensor/generators/xbuilder.hpp>


namespace CALevelGen
{
    //Resamples a coordinate from "source" space 0 to N-1, into "destination" space 0 to M-1.
    //This can be used to implement Nearest sampling of a grid onto another grid size.
    inline size_t GetNearestCoord(size_t srcSize, size_t destSize, size_t srcPixel)
    {
        //The real math is 'dest = src * (destSize / srcSize)'.
        //However, under integer divsion we need to do the multiply first to get correct results.
        return (srcPixel * destSize) / srcSize;
    }

    //splitmix64's finalizer; a good way to hash an integer.
    inline uint64_t HashU64(uint64_t z)
    {
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    //Seeding C++ rng's is a pain in the ass, and also forces a heap allocation!
    //This function mixes integer data by hand to produce two uint64's.
    template<typename... Ints>
    std::array<uint64_t, 2> MakeSeeds(Ints... inputs)
    {
        static_assert(sizeof...(Ints) > 0, "Need at least one input");
        static_assert(((std::is_integral_v<Ints> && sizeof(Ints) <= 8) && ...),
                      "Only integers up to 64 bits are supported");

        //Start from a nonzero constant -- 0 is a fixed point of HashU64.
        uint64_t h = 0x9e3779b97f4a7c15ULL;
        ((h = HashU64(h + static_cast<uint64_t>(inputs))), ...);

        return { h, HashU64(h ^ 0xd1b54a32d192ed03ULL) };
    }

    //Efficiently seeds a pcg32 RNG from any number of integers, no heap allocations needed.
    template<typename... Ints>
    inline pcg32 SeedRng(Ints... seeds)
    {
        auto inputs = MakeSeeds(seeds...);
        return { inputs[0], inputs[1] };
    }

    //Efficiently and *deterministically* (unlike std::uniform_real_distribution)
    //   generates a uniform-random float >=0 and <1,
    //   for any random engine that natively generates 32-bit output.
    //
    //Supporting it for all random engines is more annoying so I didn't bother.
    template<std::uniform_random_bit_generator G>
        requires (G::min() == 0 && G::max() == std::numeric_limits<uint32_t>::max())
    inline float Rng32Float01(G& rng)
    {
        //A 32-bit float mantissa has 24 bits of precision.
        //Thus it is 100% precise to take 24 random bits and divide by 2^24 (a power of 2),
        //   to get a float in [0, 1).
        //PCG's best bits are the upper ones, and we mainly use PCG in this project, so take those.
        return static_cast<float>(rng() >> 8) * 0x1p-24f;
    }
}