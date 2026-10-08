#include "LevelGen.hpp"

#include <glm/glm.hpp>
#include <xtensor/containers/xtensor.hpp>
#include <xtensor/generators/xbuilder.hpp>
#include <xtensor/core/xmath.hpp>


using namespace CALevelGen;
using namespace CALevelGen::LayeredWolfram2D;


bool Generator::Validate() const
{
    return NLayers > 0 && NLayers <= MaxNLayers &&
           (RuleTree.size() == ExpectedRuleTreeSize(NLayers)) &&
           (LayerUpscales.size() == NLayers - 1) &&
           //All upscale values are nonzero (negatives mean downscale)
           std::accumulate(LayerUpscales.begin(), LayerUpscales.end(),
                           true, [](bool b, int i) { return b && (i != 0); }) &&
           (LayerBorders.size() == NLayers);
}
void Generator::Sanitize()
{
    NLayers = std::clamp(NLayers, 1, MaxNLayers);

    for (auto i = RuleTree.size(); i < ExpectedRuleTreeSize(NLayers); ++i)
        RuleTree.emplace_back(0x0000efabcdef0123 * i);
    RuleTree.erase(RuleTree.begin() + ExpectedRuleTreeSize(NLayers), RuleTree.end());

    for (auto i = LayerUpscales.size(); i < NLayers - 1; ++i)
        LayerUpscales.push_back(2);
    LayerUpscales.erase(LayerUpscales.begin() + (NLayers - 1), LayerUpscales.end());
    for (int& scale : LayerUpscales)
        if (scale == 0)
            scale = 1;

    for (auto i = LayerBorders.size(); i < NLayers; ++i)
        LayerBorders.emplace_back(false);
    LayerBorders.erase(LayerBorders.begin() + NLayers, LayerBorders.end());
}

State::State(const Generator& generator, int sizeX, int sizeY,
             uint64_t seed, float startingDensity,
             std::string* outWarning)
    : GeneratorInfo(generator)
{
    //Validate and fix inputs.
    if (!GeneratorInfo.Validate())
    {
        if (outWarning)
            *outWarning += std::string("Generator settings are invalid and will be tweaked!\n");
        GeneratorInfo.Sanitize();
    }
    if (std::min(sizeX, sizeY) < 1)
    {
        if (outWarning)
            *outWarning += std::string("Grid size was <= 0! Bumping it up to at least 1x1\n");
        sizeX = std::max(sizeX, 1);
        sizeY = std::max(sizeY, 1);
    }
    if (startingDensity < 0 || startingDensity > 1)
    {
        if (outWarning)
            *outWarning += std::string("Starting density should go from 0 to 1; values outside this range are capped\n");
        startingDensity = std::clamp(startingDensity, 0.0f, 1.0f);
    }

    //Set up the initialization of each layer.
    //TODO: More options for starting state (even per-layer)
    auto pickPixel = [&](int layerI, size_t posX, size_t posY) -> bool
    {
        auto rng = SeedRng(posX, posY, layerI, seed);
        return (Rng32Float01(rng) < startingDensity);
    };

    //The size is defined in terms of the last/most detailed layer, so
    //   generate the layer states from most-detailed to least,
    //   then reverse the list to order it like the other layer data.
    int layerSizeX = sizeX,
        layerSizeY = sizeY;
    bool warnedAboutLayerSize = false;
    for (int layerI = GeneratorInfo.NLayers - 1; layerI >= 0; --layerI)
    {
        //Generate the grid.
        LayerStates.push_back(std::apply(
            xt::vectorize([&](size_t x, size_t y) { return pickPixel(layerI, x, y); }),
            xt::meshgrid(
                xt::arange<size_t>(layerSizeX),
                xt::arange<size_t>(layerSizeY)
            )
        ));

        //Downscale for the next layer.
        if (layerI > 0)
        {
            int downscale = GeneratorInfo.LayerUpscales[static_cast<size_t>(layerI - 1)];
            if (downscale > 0)
            {
                layerSizeX /= downscale;
                layerSizeY /= downscale;
            }
            else
            {
                layerSizeX *= -downscale;
                layerSizeY *= -downscale;
            }
            if (std::min(layerSizeX, layerSizeY) < 1)
            {
                if (!warnedAboutLayerSize && outWarning)
                {
                    *outWarning += std::string(
                        "A layer was downscaled to size 0! "
                          "It and all subsequent layers will be inflated to at least 1x1, "
                          "but results may be poor.\n"
                    );
                    warnedAboutLayerSize = true;
                }

                layerSizeX = std::max(layerSizeX, 1);
                layerSizeY = std::max(layerSizeY, 1);
            }
        }
    }
    std::reverse(LayerStates.begin(), LayerStates.end());

    //Set up tick buffers.
    LayerStatesBuffers = LayerStates;
    LayerParentCoordsBuffer.reserve(GeneratorInfo.NLayers - 1);

    //Cache the update behavior at each layer.
    LayerUpdatesSpacingAndOffset.resize(GeneratorInfo.NLayers);
    int updateScale = 1;
    for (int layerI = GeneratorInfo.NLayers - 1; layerI >= 0; --layerI)
    {
        /*
            Sample update regimen with physical scaling AND staggering:
                last layer:    0,    ,  scale 1, offset 0
                last-1 layer:  1,  *2,  scale 2, offset 1
                last-2 layer:  2,  *2,  scale 4, offset 2
                last-3 layer:  3,  *1,  scale 4, offset 3
                last-4 layer:  4,  *4,  scale 8, offset 4
            Ticks:
                0: {1, 0, 0, 0, 0 } = 1/5
                1: {1, 1, 0, 0, 0 } = 2/5
                2: {1, 0, 1, 0, 0 } = 2/5
                3: {1, 1, 0, 1, 0 } = 3/5
                4: {1, 0, 0, 0, 1 } = 2/5
                5: {1, 1, 0, 0, 0 } = 2/5
                6: {1, 0, 1, 0, 0 } = 2/5
                7: {1, 1, 0, 1, 0 } = 3/5
                8: {1, 0, 0, 0, 0 } = 1/5
                9: {1, 1, 0, 0, 0 } = 2/5
                10: {1, 0, 1, 0, 0 } = 2/5
                11: {1, 1, 0, 1, 0 } = 2/5
                12: {1, 0, 0, 0, 1 } = 2/5
                13: {1, 1, 0, 0, 0 } = 2/5
            Honestly not sure why it works so well, even though I wrote it...can it be improved?
            I think all layers with the same scale N should evenly distribute themselves across the offset range of 0 to N-1;
                on the other hand it's expected that scale factors are nearly always >1.
        */

        LayerUpdatesSpacingAndOffset[layerI] = std::make_tuple(
            updateScale,
            GeneratorInfo.StaggerUpdates ? ((GeneratorInfo.NLayers - 1 - layerI) % updateScale) : 0
        );
        if (layerI > 0)
        {
            if (GeneratorInfo.TimeScalesFollowPhysicalScales)
            {
                int physicalScale = GeneratorInfo.LayerUpscales[layerI - 1];
                if (physicalScale > 0)
                    updateScale *= physicalScale;
                else
                    updateScale = std::max(1, updateScale / -physicalScale);
            }
            else
            {
                updateScale *= 2;
            }
        }
    }
}

void State::Update()
{
    //Process the higher layers first since they dictate the lower layers.
    for (int layerI = 0; layerI < GeneratorInfo.NLayers; ++layerI)
    {
        auto [layerTickSpacing, layerTickOffset] = LayerUpdatesSpacingAndOffset[layerI];
        if (NTicks % layerTickSpacing == layerTickOffset)
        {
            std::array targetSize{
                LayerStates[layerI].shape()[0],
                LayerStates[layerI].shape()[1]
            };

            //Precompute the mapping from child pixel to each parent layer's pixels.
            int nParents = layerI;
            LayerParentCoordsBuffer.clear();
            for (int parentI = 0; parentI < nParents; ++parentI)
            {
                LayerParentCoordsBuffer.push_back({
                    NearestCoordMapper{
                        static_cast<uint32_t>(targetSize[0]),
                        static_cast<uint32_t>(LayerStates[parentI].shape()[0])
                    },
                    NearestCoordMapper{
                        static_cast<uint32_t>(targetSize[1]),
                        static_cast<uint32_t>(LayerStates[parentI].shape()[1])
                    }
                });
            }

            //Apply the rule at each cell.
            //The boundary behavior is chosen at compile-time to keep it out of the hot loop.
            auto makeNewStateGenerator = [&]<bool Wraps>(std::bool_constant<Wraps>, bool borderIfNotWrapped)
            {
                return [&, borderIfNotWrapped](size_t x, size_t y) -> bool
                {
                    auto ruleIdx = GeneratorInfo.PickRuleTreeIndex(
                        nParents,
                        [&](size_t parentI)
                        {
                            auto x2 = LayerParentCoordsBuffer[parentI][0](static_cast<uint32_t>(x)),
                                 y2 = LayerParentCoordsBuffer[parentI][1](static_cast<uint32_t>(y));
                            return LayerStates[parentI](x2, y2);
                        }
                    );
                    const Rule& rule = GeneratorInfo.RuleTree[ruleIdx];

                    const auto& grid = LayerStates[layerI];
                    int sizeX = static_cast<int>(targetSize[0]),
                        sizeY = static_cast<int>(targetSize[1]);

                    using U = uint_fast16_t;
                    auto makeBit = [&](int bitI, int dX, int dY) -> U
                    {
                        int nX = static_cast<int>(x) + dX,
                            nY = static_cast<int>(y) + dY;

                        bool bit;
                        if constexpr (Wraps)
                        {
                            nX += (nX < 0) ? sizeX : ((nX >= sizeX) ? -sizeX : 0);
                            nY += (nY < 0) ? sizeY : ((nY >= sizeY) ? -sizeY : 0);
                            bit = grid(nX, nY);
                        }
                        else
                        {
                            bool inBounds = (nX >= 0) && (nY >= 0) && (nX < sizeX) && (nY < sizeY);
                            bit = inBounds ? grid(nX, nY) : borderIfNotWrapped;
                        }

                        return static_cast<U>(U{ bit } << bitI);
                    };
                    U neighborBits = (
                        makeBit(0,  -1, -1) | makeBit(1,  0, -1) | makeBit(2,  1, -1) |
                        makeBit(3,  -1,  0) | makeBit(4,  0,  0) | makeBit(5,  1,  0) |
                        makeBit(6,  -1,  1) | makeBit(7,  0,  1) | makeBit(8,  1,  1)
                    );

                    return rule.GetOutputState(neighborBits);
                };
            };
            auto cellIdcs = xt::meshgrid(
                xt::arange<size_t>(targetSize[0]),
                xt::arange<size_t>(targetSize[1])
            );
            if (const auto& border = GeneratorInfo.LayerBorders[layerI]; border.has_value())
                xt::noalias(LayerStatesBuffers[layerI]) = std::apply(xt::vectorize(makeNewStateGenerator(std::false_type{}, *border)), cellIdcs);
            else
                xt::noalias(LayerStatesBuffers[layerI]) = std::apply(xt::vectorize(makeNewStateGenerator(std::true_type{}, false)), cellIdcs);

            //Immediately apply this state so that subsequent layers can see it.
            std::swap(LayerStates[layerI], LayerStatesBuffers[layerI]);
        }
    }

    NTicks += 1;
}
