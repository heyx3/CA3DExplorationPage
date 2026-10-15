#pragma once

#include <cstdint>
#include <array>
#include <utility>
#include <cassert>
#include <type_traits>
#include <random>
#include <limits>
#include <optional>
#include <span>
#include <limits>
#include <algorithm>
#include <numeric>

#include <glm/glm.hpp>

#include <pcg_random.hpp>


namespace CALevelGen
{
    //Invokes your lambda on destruction.
    template<typename F>
    struct ScopeCleanup
    {
        F&& toDo;
        ~ScopeCleanup() { toDo(); }
    };

    static constexpr bool IsEmscripten =
        #ifdef __EMSCRIPTEN__
            true
        #else
            false
        #endif
    ;


    //Precomputes an efficient transformation from a "source" pixel index range to a "destination",
    //   matching the behavior of Nearest texture filtering.
    //
    //Construct it with the source and destination pixel count,
    //   then invoke it with the source pixel index to get the destination pixel index.
    //Operates on one axis, so keep one per-component.
    //
    //Precision issues occur for source sizes above 2^16; fortunately that's not likely in our use-case.
    struct NearestCoordMapper
    {
        //Based on a neat trick: treat a uint64 as a fixed-point value,
        //  with the top 32 bits being the integer and bottom 32 bits being the fraction.
        //
        //In this format, multiplication and division are still done with more-or-less standard integer division.
        //But now division produces 32 extra bits of fractional precision!
        //
        //At runtime, you apply this ratio to a uint32 coordinate and truncate the fractional component.
        //There are some technical details to the math which I'm skimming over.

        uint64_t Multiplier_FixedPoint;
        uint32_t ClampMax; //Source sizes above 2^16 cause imprecision and may step over the dest range

        NearestCoordMapper(uint32_t srcSize, uint32_t destinationSize)
        {
            assert(srcSize > 0 && destinationSize > 0);

            auto destSize_FixedPoint = uint64_t{ destinationSize } << 32;
            //The ratio is rounded up to ensure correct results in every case.
            Multiplier_FixedPoint = (destSize_FixedPoint + srcSize - 1) / srcSize;

            ClampMax = destinationSize - 1;
        }

        uint32_t operator()(uint32_t c) const
        {
            auto result_FixedPoint = uint64_t{c} * Multiplier_FixedPoint;
            return std::min(ClampMax, static_cast<uint32_t>(result_FixedPoint >> 32));
        }
    };


    template<typename I, typename F, size_t N>
    std::optional<std::array<F, 2>> RayBoxHits(std::span<const I, N> boxMin, std::span<const I, N> boxMax,
                                               std::span<const F, N> rayStart, std::span<const F, N> rayInvDir,
                                               std::array<int, 2>* outHitFaces = nullptr)
        requires std::is_floating_point_v<F>
    {
        static_assert(N > 0, "Must be at least 1D");

        std::array<bool, N> rayAxesAreForward;
        std::array<std::array<F, 2>, N> boxStartStops;
        for (size_t i = 0; i < N; ++i)
        {
            rayAxesAreForward[i] = (rayInvDir[i] > 0);
            auto boxMaxValue = boxMax[i] + std::is_integral_v<I>;
            boxStartStops[i] = (rayAxesAreForward[i] ?
                std::array{ boxMin[i], boxMaxValue } :
                std::array{ boxMaxValue, boxMin[i] }
            );
        }

        F t0 = -std::numeric_limits<F>::infinity(),
          t1 = -t0;
        int a0 = -1,
            a1 = -1;
        for (int i = 0; i < static_cast<int>(N); ++i)
        {
            F ti_0 = (boxStartStops[i][0] - rayStart[i]) * rayInvDir[i],
              ti_1 = (boxStartStops[i][1] - rayStart[i]) * rayInvDir[i];
            if (ti_0 > t0)
            {
                a0 = i;
                t0 = ti_0;
            }
            if (ti_1 < t1)
            {
                a1 = i;
                t1 = ti_1;
            }
        }

        if (std::min(a0, a1) < 0 || t0 > t1)
            return std::nullopt;
            
        if (outHitFaces)
            *outHitFaces = { a0, a1 };
        return std::array{ t0, t1 };
    }

    //Computes and represents a 3D vector basis.
    template<typename F>
        requires std::is_floating_point_v<F>
    struct Basis3D
    {
        using V3 = glm::vec<3, F>;
        V3 Forward, Up, Right;

        Basis3D() : Forward(1, 0, 0), Up(0, 0, 1), Right(0, 1, 0) {}
        Basis3D(const V3& forward, const V3& right, const V3& up) : Forward(forward), Right(right), Up(up) { }
        Basis3D(const V3& forward, const V3& roughUpward)
            : Forward(forward)
        {
            Right = glm::normalize(glm::cross(Forward, roughUpward));
            Up = glm::normalize(glm::cross(Right, Forward));
        }
        
        //Gets a pre-multiplied matrix, mapping world-space coordinates to this basis.
        auto GetMatrix3() const { return glm::mat<3, 3, F>(GetMatrix4()); }
        //Gets a pre-multiplied matrix, mapping world-space coordinates to this basis.
        auto GetMatrix4() const {
            return glm::transpose(glm::mat<4, 4, F>( // (we want them to be rows, but each row we pass in here becomes a column)
                glm::vec<4, F>(Forward, 0),
                glm::vec<4, F>(Right, 0),
                glm::vec<4, F>(Up, 0),
                glm::vec<4, F>(0, 0, 0, 1)
        )); }
    };


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