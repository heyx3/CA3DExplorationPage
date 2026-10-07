#pragma once

#include <cstdint>
#include <array>
#include <span>
#include <tuple>
#include <utility>
#include <cassert>
#include <vector>
#include <optional>
#include <random>

#include <pcg_random.hpp>
#include <glm/glm.hpp>
#include <xtensor/containers/xtensor.hpp>
#include <xtensor/views/xview.hpp>
#include <xtensor/core/xexpression.hpp>
#include <xtensor/core/xmath.hpp>
#include <xtensor/core/xnoalias.hpp>
#include <xtensor/core/xvectorize.hpp>
#include <xtensor/generators/xbuilder.hpp>
#include <xtensor/core/xiterable.hpp>

#include "utils.hpp"


namespace CALevelGen
{
    //This generator is multiple layers of what is officially called
    //   "a two-state 2D Wolfram CA, with a radius-1 Moore neighborhood".
    //
    //More simply, this is a CA in which each cell has two possible values (encodable as a single bit),
    //   and to compute the next state each cell takes itself with its eight surrounding neighbors
    //   to build a 9-bit number.
    //This number indexes a lookup table of 2^9 bits (512), representable as a 128-digit hex number,
    //   describing the output state in every possible scenario.
    //
    //Outer CA layers are used to select what rules the inner layers should use;
    //   in other words each outer layer injects stucture
    //   by adding one bit of input state to an inner layer's cell.
    //
    //Generally each subsequent layer has twice the resolution and updates twice as often,
    //   until the final layer which runs at full resolution and updates every tick.
    namespace LayeredWolfram2D
    {
        //A grid state is represented as a 2D tensor of bool.
        //It could be an allocation, view, or expression, so they are generally taken as templated types.

        //One version of the Wolfram 2D CA with Moore Neighborhood 1.
        //Packed tightly into a bitfield, where bit i represents
        //   "output state when surounding cells are in configuration i".
        struct Rule
        {
            static constexpr size_t NBits = (2 * 2 * 2 *
                                             2 * 2 * 2 *
                                             2 * 2 * 2);
            using Bits_t = std::bitset<NBits>;

            Bits_t Definition;

            //Retrieves the correct output state for a cell, given the state of its neighborhood packed into an integer
            //   (only the first 9 bits should matter).
            template<typename I>
            bool GetOutputState(I cellConfigurationIdx) const
            {
                static_assert(sizeof(I) > 1,
                              "Integer type isn't big enough to store 9 bits of neighbor state!");
                if constexpr (std::is_unsigned_v<I>)
                {
                    //Check that only the first 9 bits were used.
                    assert(static_cast<uint64_t>(cellConfigurationIdx) < (uint64_t{1} << 9));
                    return Definition[cellConfigurationIdx];
                }
                else
                {
                    //It's a bitfield, so cast to unsigned or else we'll get negative indices.
                    return GetOutputState(static_cast<std::make_unsigned_t<I>>(cellConfigurationIdx));
                }
            }
        };
        
        struct Generator
        {
            static int ExpectedRuleTreeSize(int nLayers) { return (1 << nLayers) - 1; }
            static constexpr int MaxNLayers = 16; //Because our fast index math type is uint_fast16_t

            int NLayers = 2;
            //The rules are stored from topmost layer on down:
            //  [largest, second-largest-1, second-largest-2, ...]
            //The count is (2^NLayers)-1.
            std::vector<Rule> RuleTree = { { 0x55aabbcc00110011 }, { 0xfafafafa00110011 }, { 0x1231231231239999 } };
            //The scaling factor from each layer to the next.
            //The count is (NLayers-1).
            //
            //A good default is to use 2 at every level.
            //You may also provide negative values to indicate an *increase* in detail,
            //   but this is a weird idea that will produce weird results.
            std::vector<int> LayerUpscales = { 2 };
            //Controls the boundary behavior for each layer.
            //Null means "wrap", while true/false means a simulated border with that value.
            std::vector<std::optional<bool>> LayerBorders = { false, false };

            //Coarser layers update more slowly.
            //By default there will often be ticks where most/all layers update at once,
            //   causing jarring changes at those timestamps.
            //This flag causes layer updates to be staggered so there's usually no more than 2 layers updating at a time.
            bool StaggerUpdates = true;
            //If false, then each parent layer updates half as frequently as its child no matter what.
            //If true, then each parent's update frequency is related to its child through its upscale factor
            //   (so 2 is half as frequently, 4 is 1/4 as frequently, etc)
            bool TimeScalesFollowPhysicalScales = false;


            //Returns whether this generator is valid.
            bool Validate() const;
            //Fixes this generator as necessary (if Validate() is true, nothing changes).
            void Sanitize();

            //Decides which rule to use for a cell within a particular layer, based on the parent states above that layer.
            template<typename I, typename TGetParent>
            static auto PickRuleTreeIndex(I nParents, TGetParent&& getParent)
            {
                using U = uint_fast16_t;

                auto layerI = static_cast<U>(nParents),
                     firstDataI = static_cast<U>((U{1} << layerI) - U{1});

                U offset = 0;
                for (U parentI = 0; parentI < static_cast<U>(nParents); ++parentI)
                    offset += (U{1} << parentI) * U{ getParent(parentI) };

                return firstDataI + offset;
            }
        };

        //The current state of level generation.
        //
        //NOTE that state updates are double-buffered, so
        //   references to grid data should not live past an Update() call.
        struct State
        {
            //NOTE: You could technically change small generator parameters during a run and be fine,
            //        but you're not supposed to.
            Generator GeneratorInfo;
            int NTicks = 0;

            std::vector<xt::xtensor<bool, 2>> LayerStates, LayerStatesBuffers;
            std::vector<std::tuple<int, int>> LayerUpdatesSpacingAndOffset;

            //Constructs a new instance with randomized starting state based on the given seed.
            //
            //If you provide a warning string buffer,
            //   then any problems with the input parameters can be appended to it.
            State(const Generator& generator, int sizeX, int sizeY,
                  uint64_t seed, float startingDensity,
                  std::string* outWarning);

            //Advances the simulation forward one step.
            //Invalidates any references to the state grids!
            void Update();
        };
    }
}