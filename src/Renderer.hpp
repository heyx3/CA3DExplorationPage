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
            float AmbientStrength = 0.1f,
                  DiffuseStrength = 0.4f,
                  ShadowedStrength = 0.5f;

            //Quadratically increases the number of rays cast per-pixel, to get a less-aliased result.
            //Ths has a big effect on both quality and performance!
            uint32_t AntiAliasing = 0;

            //TODO: Height-Fog

            float SkyAlpha = 1.0f;
            //TODO: Sky gradient
            //TODO: Floor

            float Gamma = 2.2f;

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
            //How much of the render actually contains the grid along the shortest viewport axis
            //  (the longer axis has even more space).
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
        //
        //THe signature for 'gridColor' is (glm::ivec3) -> glm::vec3.
        template<typename TGridBits, typename TGridColorFn, typename TOutputXtensorRgbaU8>
        void TraceGridIsometric(const Viewport& view, const Settings& settings,
                                const TGridBits& gridBits, TGridColorFn&& gridColor,
                                TOutputXtensorRgbaU8& outRenderRgbaU8)
        {
            using namespace glm;

            std::array resolution = { outRenderRgbaU8.shape()[0], outRenderRgbaU8.shape()[1] };
            std::array gridSize = { gridBits.shape()[0], gridBits.shape()[1], gridBits.shape()[2] };
            glm::ivec3 gridSizeV{ gridSize[0], gridSize[1], gridSize[2] };
            glm::vec3 gridSizeF(gridSizeV);
            float aspectWOverH = static_cast<float>(resolution[0]) / resolution[1];

            auto camBasis = view.CalcCamera(glm::vec3(gridSizeV));

            //Grab the world-space corners of the world to compute boundaries.
            std::array<glm::vec3, 8> worldCorners;
            for (int x : { 0, 1 })
                for (int y : { 0, 1 })
                    for (int z : { 0, 1 })
                        worldCorners[x + (2 * (y + (2 * z)))] = gridSizeF * glm::vec3(x, y, z);
            auto projectWorldCorners = [&](const glm::vec3& along)
            {
                std::array<float, 8> outputs;
                for (int i = 0; i < outputs.size(); ++i)
                    outputs[i] = glm::dot(worldCorners[i], along);
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
            };

            //Compute transform matrices.
            //Output XY coordinate needs to be pixel index,
            //   but Z will remain view-depth.
            //Note that we're using premultiplied matrices here, so perform(m1, m2, m3) = m3*m2*m1
            //Also note that matrix construction is column-major,
            //   which is annoying since arguments are naturally written row-major!

            //  1. View matrix:
            auto worldToView = camBasis.GetMatrix4();
            //  2. Projection matrix (ortho):
            auto gridPosesAlongCamRight = projectWorldCorners(camBasis.Right),
                 gridPosesAlongCamUp = projectWorldCorners(camBasis.Up);
            glm::vec2 orthoMin{
                std::ranges::min(gridPosesAlongCamRight),
                std::ranges::min(gridPosesAlongCamUp)
            };
            glm::vec2 orthoMax{
                std::ranges::max(gridPosesAlongCamRight),
                std::ranges::max(gridPosesAlongCamUp)
            };
            glm::vec2 orthoSize = orthoMax - orthoMin,
                      orthoCenter = (orthoMin + orthoMax) / 2.0f;
            orthoSize /= (2 * view.ZoomOutT);
            if (orthoSize.x / orthoSize.y < aspectWOverH)
                orthoSize.x = orthoSize.y * aspectWOverH;
            else
                orthoSize.y = orthoSize.x / aspectWOverH;
            glm::mat4 viewToNorm = glm::ortho(orthoCenter.x - orthoSize.x,
                                              orthoCenter.x + orthoSize.x,
                                              orthoCenter.y - orthoSize.y,
                                              orthoCenter.y + orthoSize.y);
            viewToNorm[2][2] = 1; //Remove GLM's Z flip
            //  3. Window matrix (NDC to pixel, but leaving Z as view-depth):
            auto normToPixel = (
                // c) Scale by pixel count
                glm::mat4(
                    resolution[0], 0, 0, 0,
                    0, resolution[1], 0, 0,
                    0, 0, 1, 0,
                    0, 0, 0, 1
                ) *
                // b) Shift to 0,1
                glm::mat4(
                    1, 0, 0, 0,
                    0, 1, 0, 0,
                    0, 0, 1, 0,
                    0.5, 0.5, 0, 1
                ) *
                // a) Scale from -1,1 to -0.5,0.5
                glm::mat4(
                    0.5, 0, 0, 0,
                    0, 0.5, 0, 0,
                    0, 0, 1, 0,
                    0, 0, 0, 1
                )
            );

            glm::mat4 worldToPixelAndDepth = normToPixel * viewToNorm * worldToView,
                      pixelAndDepthToWorld = glm::inverse(worldToPixelAndDepth);
            float rayStartViewDepth = std::ranges::min(projectWorldCorners(camBasis.Forward));
            rayStartViewDepth -= 1; //Prevent any chance of starting inside the grid
            double aaSampleWeight = 1.0 / ((settings.AntiAliasing + 1) * (settings.AntiAliasing + 1));
            for (uint32_t y = 0; y < resolution[1]; ++y)
                for (uint32_t x = 0; x < resolution[0]; ++x)
                {
                    glm::dvec4 aaSum(0);
                    for (uint32_t aaY = 0; aaY < settings.AntiAliasing + 1; ++aaY)
                        for (uint32_t aaX = 0; aaX < settings.AntiAliasing + 1; ++aaX)
                        {
                            //Get ray data.
                            glm::vec2 subPixelOffset = (glm::vec2(aaX, aaY) + 1.0f) /
                                                       glm::vec2(settings.AntiAliasing + 2);
                            glm::vec3 pixelAndDepth(glm::vec2(x, y) + subPixelOffset,
                                                    rayStartViewDepth);
                            glm::vec3 rayOrigin = (pixelAndDepthToWorld * glm::vec4(pixelAndDepth, 1)).xyz(),
                                      rayDir = camBasis.Forward;

                            //Execute the march.
                            std::optional<glm::ivec3> hitCell;
                            glm::vec3 hitPos;
                            int hitAxis = -1;
                            RunDDA(
                                rayOrigin, rayDir, glm::ivec3(gridSize[0], gridSize[1], gridSize[2]),
                                [&](const glm::ivec3& cellIdx, const glm::vec3& rayEntryPos, float t, std::optional<int> axisEnteredThrough)
                                {
                                    if (gridBits(cellIdx.x, cellIdx.y, cellIdx.z))
                                    {
                                        hitCell = cellIdx;
                                        hitPos = rayEntryPos;
                                        hitAxis = *axisEnteredThrough;
                                        return false;
                                    }
                                    return true;
                                },
                                [&](bool missedGridEntirely) { }
                            );

                            if (hitCell.has_value())
                            {
                                glm::vec3 normal(0);
                                normal[hitAxis] = -glm::sign(rayDir[hitAxis]);

                                glm::vec3 albedo = gridColor(*hitCell);

                                //Compute lighting.
                                float shadowedLight = 1;
                                if (settings.ShadowedStrength > 0)
                                {
                                    RunDDA(
                                        hitPos + (normal * 0.001f),
                                        settings.DirTowardsSun,
                                        gridSizeV,
                                        [&](const glm::ivec3& shadowCellIdx,
                                            const glm::vec3& shadowRayEntryPos,
                                            float shadowRayT,
                                            std::optional<int> shadowAxisEnteredThrough)
                                        {
                                            if (gridBits(shadowCellIdx.x, shadowCellIdx.y, shadowCellIdx.z))
                                            {
                                                shadowedLight = 0;
                                                return false;
                                            }
                                            return true;
                                        },
                                        [&](bool missedGridEntirely) { shadowedLight = 1.0f - glm::clamp(settings.ShadowedStrength, 0.0f, 1.0f); }
                                    );
                                }
                                float diffuseLight = std::max(0.0f, glm::dot(normal, settings.DirTowardsSun));
                                float totalLight = glm::clamp(
                                    settings.AmbientStrength +
                                      (diffuseLight * (settings.DiffuseStrength + shadowedLight)),
                                    0.0f, 1.0f
                                );

                                glm::vec3 finalColor = albedo * totalLight;

                                aaSum += glm::vec4(finalColor, 1);
                            }
                            else
                            {
                                //Use premultiplied alpha for the sky color, to prevent sky AA samples bleeding into voxel AA samples.
                                aaSum += settings.SkyAlpha * glm::vec4(
                                    settings.GetSkyColor(
                                        pixelAndDepth.xy() / glm::vec2(resolution[0], resolution[1]),
                                        rayDir
                                    ),
                                    1
                                );
                            }
                        }

                    glm::dvec4 finalLinearColor = aaSum * aaSampleWeight;
                    finalLinearColor.xyz() = glm::pow(finalLinearColor.xyz(), glm::dvec3(1.0 / settings.Gamma));

                    outRenderRgbaU8(x, y) = static_cast<glm::u8vec4>(glm::clamp(
                        finalLinearColor * 255.0,
                        glm::dvec4(0),
                        glm::dvec4(255)
                    ));
                }
        }
    }
}