include(FetchContent)

#Third-party libraries are built from source for whichever toolchain is active,
#   which is why this doesn't use find_package: a Windows install can't be linked into wasm.
#Pin an exact tag for each, so builds are reproducible.

#Header-only libraries, so skip their tests/install rules.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW ON)
FetchContent_Declare(xtl
    GIT_REPOSITORY https://github.com/xtensor-stack/xtl.git
    GIT_TAG 0.8.2
    GIT_SHALLOW ON)
FetchContent_Declare(xtensor
    GIT_REPOSITORY https://github.com/xtensor-stack/xtensor.git
    GIT_TAG 0.27.1
    GIT_SHALLOW ON)

#PCG has no CMake support, so FetchContent only downloads it (see the target below).
#The latest tag (v0.98.1) predates its MSVC fixes, so this pins the commit that has them.
#   Pinning a commit hash means a full clone: GIT_SHALLOW only works with a branch or tag.
FetchContent_Declare(pcg
    GIT_REPOSITORY https://github.com/imneme/pcg-cpp.git
    GIT_TAG 428802d1a5634f96bcd0705fab379ff0113bcf13)
#It seems like Dear ImGUI doesn't have Cmake support either: https://github.com/ocornut/imgui/issues/8896
FetchContent_Declare(dearimgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9)

#xtl must come before xtensor, which looks for its target.
FetchContent_MakeAvailable(glm xtl xtensor pcg dearimgui)

#A header-only target for PCG, marked SYSTEM (see below).
add_library(pcg INTERFACE)
add_library(pcg::pcg ALIAS pcg)
target_include_directories(pcg SYSTEM INTERFACE ${pcg_SOURCE_DIR}/include)

#Dear ImGUI:
add_library(dearimgui STATIC
    ${dearimgui_SOURCE_DIR}/imgui.cpp
    ${dearimgui_SOURCE_DIR}/imgui_demo.cpp
    ${dearimgui_SOURCE_DIR}/imgui_draw.cpp
    ${dearimgui_SOURCE_DIR}/imgui_tables.cpp
    ${dearimgui_SOURCE_DIR}/imgui_widgets.cpp
    ${dearimgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${dearimgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp)
add_library(dearimgui::dearimgui ALIAS dearimgui)
target_include_directories(dearimgui SYSTEM PUBLIC ${dearimgui_SOURCE_DIR})
target_include_directories(dearimgui SYSTEM PUBLIC ${dearimgui_SOURCE_DIR}/backends)
target_compile_definitions(dearimgui PUBLIC IMGUI_IMPL_OPENGL_ES3)


#Mark the third-party headers as SYSTEM includes.
#   MSVC then treats them as "external": no compiler warnings from them,
#   and Visual Studio can skip code analysis on them.
function(MarkAsSystemHeaders targetName)
    if(NOT TARGET ${targetName})
        return()
    endif()
    get_target_property(aliased ${targetName} ALIASED_TARGET)
    if(aliased)
        set(targetName ${aliased})
    endif()
    get_target_property(dirs ${targetName} INTERFACE_INCLUDE_DIRECTORIES)
    if(dirs)
        set_target_properties(${targetName} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dirs}")
    endif()
endfunction()

#GLM keeps its include path on this target, not on glm::glm.
MarkAsSystemHeaders(glm-header-only)

#GLM's compile-time switches go on its target, so every file that uses GLM (and IntelliSense)
#   sees the same settings. They must be identical across all translation units.
#Swizzling is written as a function call, v.xy(), on every compiler.
#   (GLM can only fake member-style v.xy with a nonstandard anonymous-union extension,
#   which it turns on for MSVC but not for Emscripten's Clang.)
#   XYZW_ONLY drops rgba/stpq and SIMD, and is what makes GLM use the function form everywhere.
target_compile_definitions(glm-header-only INTERFACE GLM_FORCE_SWIZZLE GLM_FORCE_XYZW_ONLY)
MarkAsSystemHeaders(xtl)
MarkAsSystemHeaders(xtensor)
MarkAsSystemHeaders(dearimgui)