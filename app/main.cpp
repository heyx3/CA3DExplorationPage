#include "LevelGen.hpp"

#include <iostream>

#include <glm/vec4.hpp>

#include <SDL.h>
#include <GLES3/gl3.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>


static constexpr bool IsEmscripten =
#ifdef __EMSCRIPTEN__
    true;

    #include <emscripten.h>
    #include <emscripten/html5.h>
    //#include "../libs/emscripten/emscripten_mainloop_stub.h"

    EM_JS(void, BlitRGBA8, (const void* pixels, int width, int height), {
        const canvas = Module.canvas;
        if (canvas.width != width || canvas.height != height) {
            canvas.width = width;
            canvas.height = height;
        }

        //View straight into WASM memory, avoiding a copy.
        //Recreated on every call in case memory location has changed.
        const data = new Uint8ClampedArray(HEAPU8.buffer, pixels, width * height * 4);
        canvas.getContext('2d').putImageData(new ImageData(data, width, height), 0, 0);
    });

#else
    false;

    static void BlitRGBA8(const void* pixels, int width, int height) { std::cerr << "(blit " << width << "x" << height << " pixels)\n"; }

#endif


namespace
{
    using namespace CALevelGen;

    static LayeredWolfram2D::Generator gen;

    static constexpr int sizeX = 128,
                         sizeY = 128;
    static std::optional<LayeredWolfram2D::State> state;

    bool TrySetUpGenerator()
    {
        pcg32 rng{ 0x4556a0b4546 };
        for (LayeredWolfram2D::Rule& rule : gen.RuleTree)
        {
            for (int wordI = 0; wordI < (rule.Definition.size() / 8) / sizeof(uint32_t); ++wordI)
            {
                uint32_t newBits = rng();
                for (int bitI = 0; bitI < 32; ++bitI)
                    rule.Definition[bitI + (32 * wordI)] = (newBits >> bitI) & 0x1;
            }
        }

        std::string warnings;
        state.emplace(gen, sizeX, sizeY, rng(), 0.4f, &warnings);
        if (!warnings.empty())
        {
            std::cout << "CA STARTUP WARNINGS: " << warnings << "\n";
            return false;
        }

        return true;
    }

    static std::vector<glm::u8vec4> outRGBA = std::vector<glm::u8vec4>(sizeX * sizeY);
    void ReRender()
    {
        //Note that there are memory ordering differences between our xtensor usage and canvas images.
        for (int y = 0; y < sizeY; ++y)
            for (int x = 0; x < sizeX; ++x)
            {
                bool b = state->LayerStates.back()(x, y);
                outRGBA[x + (y * sizeX)] = glm::u8vec4{
                    b, b, b,
                    1
                } *uint8_t{ 255 };
            }
    }
}

namespace
{
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    float displayScale = -1;

    bool TrySetUpSDLandGL()
    {
        SDL_Init(SDL_INIT_VIDEO);
        
        displayScale = ImGui_ImplSDL2_GetContentScaleForDisplay(0);
        int surfaceWidth = static_cast<int>(1000 * displayScale),
            surfaceHeight = static_cast<int>(1000 * displayScale);

        auto windowFlags = static_cast<SDL_WindowFlags>(
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL
        );
        window = SDL_CreateWindow("CellularAutomata level generator",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            surfaceWidth, surfaceHeight, windowFlags);
        if (!window)
        {
            std::cerr << "Failed to create SDL window! " << SDL_GetError() << "\n";
            return false;
        }

        glContext = SDL_GL_CreateContext(window);

        return true;
    }

    void SetUpGUI()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& guiIO = ImGui::GetIO();
        guiIO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        auto& guiStyle = ImGui::GetStyle();
        guiStyle.ScaleAllSizes(displayScale);
        guiStyle.FontScaleDpi = displayScale;

        ImGui_ImplSDL2_InitForOpenGL(window, glContext);
        ImGui_ImplOpenGL3_Init();
    }
}

void TickProgram()
{
    //Process OS/browser events.
    SDL_Event newEvent;
    auto hasEvent = static_cast<bool>(SDL_PollEvent(&newEvent));
    if (hasEvent && !ImGui_ImplSDL2_ProcessEvent(&newEvent))
    {
        //Event wasn't swallowed by the GUI; handle it here.
        //NOTE: Do still check io.WantCaptureMouse and io.WantCaptureKeyboard.
    }

    //Start the frame.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //Add our own app logic.
    state->Update();
    ReRender();
    //TODO: Upload texture, display image in Dear ImGUI

    //Finish the frame.
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
}


int main()
{
    if (!TrySetUpSDLandGL())
        return 1;
    SetUpGUI();

    //For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    //You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    if (IsEmscripten)
        ImGui::GetIO().IniFilename = nullptr;


    if (!TrySetUpGenerator())
        return 2;

    //Enter the main loop.
    #ifdef __EMSCRIPTEN__
    #else
    #endif

    return 0;
}
