#pragma once

#include "utils.hpp"
#include "TextureRGBA8.hpp"
#include "Renderer.hpp"
#include "CA_LayeredMoore.hpp"

#include <string>
#include <iostream>
#include <thread>
#include <chrono>

#include <glm/vec4.hpp>

#ifdef __EMSCRIPTEN__
    #include <GLES3/gl3.h>
#else
    #include <GL/glew.h>
#endif
#include <SDL.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>


namespace CALevelGen
{
    //The core app logic, within a Dear ImGUI + OpenGL + SDL context.
    namespace Interface
    {
        //A managed OpenGL texture displaying some kind of voxel output.
        class RenderedGrid3D : public TextureRGBA8
        {
        public:
            Rendering::Settings RenderSettings;
            Rendering::Viewport RenderView;

            xt::xtensor<bool, 3> GridState;

            using ColorScheme_t = std::function<glm::vec3(glm::ivec3)>;
            virtual ColorScheme_t GetColorScheme() const
            {
                return [](const glm::ivec3&) { return glm::vec3(1.0f, 1.0f, 0.4f);  };
            }

            RenderedGrid3D(const glm::ivec3& gridSize, const glm::ivec2& renderSize);
            virtual void UpdateGrid(int nTicks) = 0;

            void UpdateRender();
        };

        //A managed OpenGL texture driven by a Layered Moore cellular automata.
        class RenderedLayeredMoore : public RenderedGrid3D
        {
        public:

            RenderedLayeredMoore(const glm::ivec3& gridSize, const glm::ivec2& renderSize,
                                 uint64_t seed, uint64_t colorSeed,
                                 float startingDensity,
                                 const LayeredMoore::Generator* generator = nullptr);

            virtual ColorScheme_t GetColorScheme() const override;
            virtual void UpdateGrid(int nTicks) override;

            const auto& GetState() const { return state; }

            auto GetNHues() const { return hues.size(); }
            const auto& GetHue(size_t i) const { return hues[i]; }
            auto& GetHue(size_t i) { return hues[i]; }

        private:

            LayeredMoore::State state;
            //For each parent, stores the state over a set time period.
            //The innermost state is stored in the inherited field 'GridState'.
            std::vector<xt::xtensor<bool, 3>> parentStateSpacetime;
            int nextTimeIdx;

            //For each possible parent state influencing the innermost layer,
            //   this stores a corresponding render color.
            //The number of entries should be 2^(N-1) where N is the number of layers.
            std::vector<glm::vec3> hues;
        };


        //Top-level management of libraries (SDL, OpenGL, Dear ImGUI) using RAII.
        class App
        {
        public:

            SDL_Window* window;
            SDL_GLContext glContext;

            float displayScale;
            glm::vec3 clearColor;

            App(glm::vec3 clearColor, bool* successFlag = nullptr);
            ~App();

            void StartFrame();
            void EndFrame();
        };

        //All the data driving the interface.
        class State
        {
        public:
            App& appContext;

            LayeredMoore::Generator gen;
            LayeredMoore::State state;

            float elapsedSeconds = 0,
                  timeOfLastIteration = -1,
                  currentAvgFPS = -1,
                  genIterRate = 10;
            uint32_t seed = 0x4556a046;

            std::vector<glm::u8vec4> rgbaBuffer;
            TextureRGBA8 rgbaTex;

            std::string warningMsg;


            State(App& app, int initialStateSizeX, int initialStateSizeY);
            ~State();

            //Updates app logic and issues Dear ImGUI commands.
            //Returns whether the app should keep running.
            bool Tick(float deltaSeconds);

        private:
            void IterateSim();
            void ResizeGenerator(int newSizeX, int newSizeY);
        };
    }
}