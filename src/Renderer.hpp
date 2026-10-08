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

#include <glm.hpp>
#include <glm/gtc/type_precision.hpp>

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
    namespace Rendering
    {
        struct Settings
        {
            glm::vec3 DirTowardsSun = glm::normalize(glm::vec3(0.25, 0.75, 1));
            float AmbientStrength = 0.1f;
            float ShadowedStrength = 0.9f;

            //TODO: Height-Fog

            //TODO: CPU-friendly post-settings (gamma, )
        };
        struct Viewport
        {
            //The isometric camera will face the scene from one of the four corners around it.
            //This describes which corner.
            glm::bvec2 CamCorner = { false, false };
        };

        //Given a grid of "solid" bools, a parallel grid of diffuse colors,
        //   and a set of render settings, generates an isometric CPU render of the given grid.
        template<typename TGridBits, typename TGridColors, typename TOutputXtensorRgbU8>
        void TraceGridIsometric(const Viewport& view, const Settings& settings,
                                const TGridBits& gridBits, const TGridColors& gridColors,
                                TOutputXtensorRgbU8& outRender)
        {
            //TODO: Implement
        }
    }
}