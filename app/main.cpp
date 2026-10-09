#include "LevelGen.hpp"
#include "TextureRGBA8.hpp"
#include "Interface.hpp"

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


static std::optional<CALevelGen::Interface::App> app;
static std::optional<CALevelGen::Interface::State> guiState;
static double lastTime = nan(nullptr);

int main(int, char*[])
{
    bool appStarted;
    app.emplace(glm::vec3(0.1f, 0.2f, 0.3f), &appStarted);
    if (!appStarted)
        return 1;

    guiState.emplace(*app, 256, 256);

    //Enter the main loop.
    #ifdef __EMSCRIPTEN__
        emscripten_request_animation_frame_loop([](double time, void*) {
            double deltaSeconds;
            if (isnan(lastTime))
                deltaSeconds = 0.01;
            else
                deltaSeconds = time - lastTime;
            lastTime = time;

            return guiState->Tick(static_cast<float>(deltaSeconds));
        }, nullptr);

    #else
        int lastFrameTime = SDL_GetTicks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        while (true)
        {
            int frameTime = SDL_GetTicks();
            float deltaSeconds = (frameTime - lastFrameTime) / 1000.0f;
            lastFrameTime = frameTime;

            if (!guiState->Tick(deltaSeconds))
                break;
        }
    #endif

    return 0;
}
