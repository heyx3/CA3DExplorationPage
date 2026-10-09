#include "Interface.hpp"

using namespace CALevelGen;
using namespace CALevelGen::Interface;

#include "utils.hpp"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>


App::App(glm::vec3 _clearColor, bool* successFlag)
    : clearColor(_clearColor)
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
        if (successFlag)
            *successFlag = false;
        return;
    }

    glContext = SDL_GL_CreateContext(window);

    #ifndef __EMSCRIPTEN__
        //GLEW can only load the GL functions once a context exists.
        GLenum glewResult = glewInit();
        if (glewResult != GLEW_OK)
        {
            std::cerr << "Failed to initialize GLEW! " << glewGetErrorString(glewResult) << "\n";
            if (successFlag)
                *successFlag = false;
            return;
        }
    #endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& guiIO = ImGui::GetIO();
    guiIO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    //For an Emscripten build we are disabling file-system access,
    //   so don't attempt to do a fopen() of the imgui.ini file.
    //You may load settings manually with LoadIniSettingsFromMemory().
    if (IsEmscripten)
        guiIO.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    auto& guiStyle = ImGui::GetStyle();
    guiStyle.ScaleAllSizes(displayScale);
    guiStyle.FontScaleDpi = displayScale;

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init();

    if (successFlag)
        *successFlag = true;
}
App::~App()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void App::StartFrame()
{
    //Set up the new frame.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    glClearColor(clearColor.x, clearColor.y, clearColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void App::EndFrame()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
}

State::State(App& _app, int stateSizeX, int stateSizeY)
    : appContext(_app),
      state(gen, 1, 1, 0x1, 0.1f, nullptr),
      rgbaTex(stateSizeX, stateSizeY, true)
{
    //Set up a random generator.
    pcg32 rng{ seed };
    for (LayeredWolfram2D::Rule& rule : gen.RuleTree)
    {
        for (int wordI = 0; wordI < (rule.Definition.size() / 8) / sizeof(uint32_t); ++wordI)
        {
            uint32_t newBits = rng();
            for (int bitI = 0; bitI < 32; ++bitI)
                rule.Definition[bitI + (32 * wordI)] = (newBits >> bitI) & 0x1;
        }
    }
    state = { gen, stateSizeX, stateSizeY, rng(), 0.5f, &warningMsg };
    if (!warningMsg.empty())
        warningMsg = std::string("ERROR setting up initial generator:\n") + warningMsg;

    rgbaBuffer.resize(stateSizeX * stateSizeY);
}
State::~State()
{
    
}

void State::ResizeGenerator(int newX, int newY)
{
    pcg32 rng{ seed };

    warningMsg.clear();
    state = { gen, newX, newY, rng(), 0.5f, &warningMsg };
    if (!warningMsg.empty())
        warningMsg = std::string("ERROR resizing generator:\n") + warningMsg;

    rgbaBuffer.resize(newX * newY);
    rgbaTex = { newX, newY, true };
}

bool State::Tick(float deltaSeconds)
{
    appContext.StartFrame();
    ScopeCleanup _{ [&]() { appContext.EndFrame(); } };

    //Process OS/browser events.
    SDL_Event newEvent;
    auto hasEvent = static_cast<bool>(SDL_PollEvent(&newEvent));
    if (hasEvent && !ImGui_ImplSDL2_ProcessEvent(&newEvent))
    {
        //Event wasn't swallowed by the GUI; handle it here.
        //NOTE: Do still check io.WantCaptureMouse and io.WantCaptureKeyboard.

        if (newEvent.type == SDL_QUIT || newEvent.type == SDL_APP_TERMINATING)
            return false;
    }

    elapsedSeconds += deltaSeconds;
    if (timeOfLastIteration < elapsedSeconds - (1.0f / genIterRate))
        IterateSim();

    ImGui::Begin("Display"); {
        rgbaTex.DrawInImGui(2);
    } ImGui::End();

    return true;
}
void State::IterateSim()
{
    timeOfLastIteration = elapsedSeconds;
    state.Update();

    for (int y = 0; y < rgbaTex.Height(); ++y)
        for (int x = 0; x < rgbaTex.Width(); ++x)
        {
            bool b = state.LayerStates.back()(x, y);
            rgbaBuffer[x + (y * rgbaTex.Width())] = glm::u8vec4{
                b, b, b,
                1
            } *uint8_t{ 255 };
        }
    rgbaTex.Update(rgbaBuffer);
}