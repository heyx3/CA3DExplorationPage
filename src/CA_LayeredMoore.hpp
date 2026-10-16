#pragma once

#include <cstdint>
#include <array>
#include <tuple>
#include <utility>
#include <cassert>
#include <vector>
#include <optional>
#include <string>
#include <algorithm>
#include <random>
#include <bitset>

#include <pcg_random.hpp>
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
    //This generator is multiple layers of what is officially called "a two-state, radius-1 Moore CA".
    //In other words, each layer is a simple two-state Cellular Automata defined as a lookup table,
    //   mapping current-state to new-state.
    //
    //The current-state is a 9-bit number (the cell itself plus its 8 neighbors, with each cell using 1 bit),
    //   so the lookup table is a 2^9-bit number (512 bits -- mapping each of the 2^9 possible state numbers to an output bit).
    //In hex the table is 128 digits long!
    //
    //The outermost CA layer is very low-resolution, in both space and time, and runs completely independently.
    //The next layer is usually higher-res, and switches between two rules based on the state of the corresponding parent cell
    //   (conceptually, the parent adds 1 new bit to the input state).
    //The third layer is even higher-res, and switches between *four* rules
    //   based on the state of its parent and grandparent cell (conceptually, 2 bits of extra input state).
    //Repeat until reaching the full-resolution innermost layer,
    //   which has N parents and 2^N rulesets to pick from based on all parents' states.
    //
    //This construction allows novel structure to be injected at multiple levels,
    //   potentially producing amazing results.
    //However it also has an unfathomably-large search space!
    //I ran multiple searches to quantify different kinds of 512-bit rulesets, defining a taxonomy for them.
    //But I encourage you to run your own search -- expect totally different kinds of results!
    namespace LayeredMoore
    {
        //A grid state is represented as a 2D tensor of bool.
        //It could be an allocation, view, or expression, so they are generally taken as templated types.

        //One 512-bit ruleset definition.
        //Packed tightly into a bitfield, where bit i represents
        //   "output state when surrounding cells are in configuration i".
        //
        //The configuration bitfield is ordered row by row, e.g:
        //  * The first bit is neighbor (-1, -1)
        //  * The second bit is neighbor (0, -1)
        //  * The seventh bit is neighbor (-1, +1)
        //  * The ninth bit is neighbor (+1, +1)
        struct SingleRule
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
        
        //The entire definition of the layered CA.
        struct Generator
        {
            static int ExpectedRuleTreeSize(int nLayers) { return (1 << nLayers) - 1; }
            static constexpr int MaxNLayers = 16; //Because our fast index math type is uint_fast16_t

            int NLayers = 2;
            //The rules are stored from topmost layer on down:
            //  [largest, second-largest-1, second-largest-2, ...]
            //The count is (2^NLayers)-1.
            std::vector<SingleRule> RuleTree = { { 0x55aabbcc00110011 }, { 0xfafafafa00110011 }, { 0x1231231231239999 } };
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
            //Fix anything that causes Validate() to return false.
            void Sanitize();

            //Compresses a line of parent cell states into an index.
            template<typename I, typename TGetParent>
            static auto PackParentStates(I nParents, TGetParent&& getParent)
            {
                using U = uint_fast16_t;
                assert(nParents <= 16);

                U u = 0;
                for (U parentI = 0; parentI < static_cast<U>(nParents); ++parentI)
                    u += (U{1} << parentI) * U{getParent(parentI)};

                return u;
            }
            //Decides which rule to use for a cell within a particular layer, based on the parent states above that layer.
            template<typename I, typename TGetParent>
            static auto PickRuleTreeIndex(I nParents, TGetParent&& getParent)
            {
                auto offset = PackParentStates(nParents, getParent);
                using U = decltype(offset);

                auto layerI = static_cast<U>(nParents),
                     firstDataI = static_cast<U>((U{1} << layerI) - U{1});
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

            //Helpfully maps coordinates of the innermost layer state to coordinates of the various parents.
            //Contains NLayers-1 elements.
            std::vector<std::array<NearestCoordMapper, 2>> LayerParentCoordsBuffer;

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

        //TODO: Binary "moore families" file and format
    }
}