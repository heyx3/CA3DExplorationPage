#pragma once

#include "utils.hpp"
#include "TextureRGBA8.hpp"
#include "LevelGen.hpp"

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

            LayeredWolfram2D::Generator gen;
            LayeredWolfram2D::State state;

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