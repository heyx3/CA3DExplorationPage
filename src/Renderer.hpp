#pragma once

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
#include <ranges>

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/geometric.hpp>

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

            //Quadratically increases the number of rays cast per-pixel, to get a less-aliased result.
            //Ths has a big effect on both quality and performance!
            uint32_t AntiAliasing = 0;

            //TODO: Height-Fog

            //TODO: Sky gradient
            //TODO: Floor

            //TODO: CPU-friendly post-settings (gamma, )

            glm::vec3 GetSkyColor(const glm::vec2& screenUV, const glm::vec3& orthoViewDir) const
            {
                return glm::mix(
                    glm::vec3(0.9, 0.9, 1.0),
                    glm::vec3(0.2, 0.2, 1.0),
                    glm::max(orthoViewDir.z, 0.0f)
                );
            }
        };
        struct Viewport
        {
            //The isometric camera will face the scene from one of the four corners around it.
            //This describes which corner.
            glm::bvec2 CamCorner = { false, false };
            //Roughly how much of the render actually contains the grid; the rest is backdrop.
            float ZoomOutT = 0.7f;

            Basis3D<float> CalcCamera(const glm::vec3& gridSize) const
            {
                glm::vec2 horzForward = glm::mix(glm::vec2(1, 1), -glm::vec2(1, 1), CamCorner);
                return {
                    glm::normalize(gridSize * glm::vec3(horzForward, -1)),
                    glm::vec3(0, 0, 1)
                };
            }
        };

        //Given a grid of "solid" bools, a parallel grid of diffuse colors,
        //   and a set of render settings, generates an isometric CPU render of the given grid.
        //
        //The vertical axis will be Z.
        template<typename TGridBits, typename TGridColors, typename TOutputXtensorRgbU8>
        void TraceGridIsometric(const Viewport& view, const Settings& settings,
                                const TGridBits& gridBits, const TGridColors& gridColors,
                                TOutputXtensorRgbU8& outRender)
        {
            using namespace glm;

            std::array resolution = { outRender.shape()[0], outRender.shape()[1] };
            std::array gridSize = { gridBits.shape()[0], gridBits.shape()[1], gridBits.shape()[2] };
            auto camBasis = view.CalcCamera({ gridSize[0], gridSize[1], gridSize[2] });
            assert(gridSize[0] == gridColors.shape()[0] &&
                   gridSize[1] == gridColors.shape()[1] &&
                   gridSize[2] == gridColors.shape()[2]);

            //Grab the world-space corners of the world to compute boundaries.
            std::array<glm::vec3, 8> worldCorners;
            for (int x : { 0, gridSize[0] })
                for (int y : { 0, gridSize[1] })
                    for (int z : { 0, gridSize[2] })
                        worldCorners[x + (2 * (y + (2 * z)))] = { x, y, z };
            auto projectWorldCorners = [&](const glm::vec3& along)
            {
                std::array<float, 8> outputs;
                for (int i = 0; i < outputs.size(); ++i)
                    outputs[i] = glm::dot(worldCorners, along);
                return outputs;
            };
            auto transformWorldCorners = [&](const glm::mat4& tr, bool isPos)
            {
                auto outputs = worldCorners;
                for (auto& v : outputs)
                {
                    auto v4 = tr * vec4(v, isPos);
                    v = v4.xyz() / v4.w;
                }
                return outputs;
            }

            //Compute transform matrices.
            //Output XY coordinate needs to be pixel index,
            //   but Z will remain view-depth.
            //Note that we're using premultiplied matrices here, so perform(m1, m2, m3) = m3*m2*m1
            //Also note that matrix construction is column-major,
            //   which is annoying since arguments are naturally written row-major!

            //  1. View matrix:
            auto worldToView = camBasis.GetMatrix4();
            //  2. Projection matrix (ortho):
            auto gridPosesAlongCamRight = projectWorldCorners(camRight),
                 gridPosesAlongCamUp = projectWorldCorners(camUp);
            glm::vec2 orthoMin{
                std::min_element(gridPosesAlongCamRight.begin(), gridPosesAlongCamRight.end()),
                std::min_element(gridPosesAlongCamUp.begin(), gridPosesAlongCamUp.end())
            };
            glm::vec2 orthoMax{
                std::max_element(gridPosesAlongCamRight.begin(), gridPosesAlongCamRight.end()),
                std::max_element(gridPosesAlongCamUp.begin(), gridPosesAlongCamUp.end())
            };
            glm::vec2 orthoSize = orthoMax - orthoMin,
                      orthoCenter = (orthoMin + orthoMax) / 2.0f;
            float zoomScale = std::sqrt(ZoomOutT) / 2.0f;
            glm::mat4 viewToNorm = glm::ortho(orthoCenter.x - (orthoSize.x * zoomScale),
                                              orthoCenter.x + (orthoSize.x * zoomScale),
                                              orthoCenter.y - (orthoSize.y * zoomScale),
                                              orthoCenter.y + (orthoSize.y * zoomScale));
            //  3. Window matrix (NDC to pixel, but leaving Z as view-depth):
            auto normToPixel = (
                // c) Scale by pixel count
                glm::mat4(
                    resolution[0], 0, 0, 0,
                    0, resolution[1], 0, 0,
                    0, 0, 1, 0,
                    0, 0, 0, 1
                ) *
                // b) Scale from -1,1 to -0.5,0.5
                glm::mat4(
                    0.5, 0, 0, 0,
                    0, 0.5, 0, 0,
                    0, 0, 1, 0,
                    0, 0, 0, 1
                ) *
                // a) Shift to 0,1
                glm::mat4(
                    1, 0, 0, 0,
                    0, 1, 0, 0,
                    0, 0, 1, 0,
                    0.5, 0.5, 0, 1
                )
            );

            glm::mat4 worldToPixelAndDepth = normToPixel * viewToNorm * worldToView,
                      pixelAndDepthToWorld = glm::inverse(worldToPixelAndDepth);
            glm::vec3 invRayDir = 1.0f / camForward;
            double aaSampleWeight = 1.0 / ((settings.AntiAliasing + 1) * (settings.AntiAliasing + 1));
            for (int y = 0; y < resolution[1]; ++y)
                for (int x = 0; x < resolution[0]; ++x)
                {
                    glm::dvec3 aaSum = 0;
                    for (int aaY = 0; aaY < settings.AntiAliasing + 1; ++aaY)
                        for (int aaX = 0; aaX < settings.AntiAliasing + 1; ++aaX)
                        {
                            //Get ray data.
                            glm::vec2 subPixelOffset = (glm::vec2(aaX, aaY) + 1) /
                                                       glm::vec2(settings.AntiAliasing + 2);
                            glm::vec3 pixelAndDepth(glm::vec2(x, y) + subPixelOffset,
                                                    0);
                            glm::vec3 rayOrigin = (pixelAndDepthToWorld * pixelAndDepth).xyz(),
                                      rayDir = camForward;

                            //Check intersection with voxel grid.
                            std::array<int, 2> gridFaceHits;
                            auto gridHit = RayBoxHits(std::array{ 0.0f, 0.0f, 0.0f },
                                                      std::array<float>{ gridSize[0], gridSize[1], gridSize[2] },
                                                      glm::value_ptr(rayOrigin), glm::value_ptr(invRayDir),
                                                      &gridFaceHits);
                            if (!gridHit.has_value())
                            {
                                aaSum += settings.GetSkyColor(pixelAndDepth.xy() / glm::vec2(resolution[0], resolution[1]),
                                                              rayDir);
                                continue;
                            }

                            //TODO: DDA and shade
                        }

                    outRender(x, y) = static_cast<glm::vec3>(aaSum * aaSampleWeight);
                }
        }
    }
}