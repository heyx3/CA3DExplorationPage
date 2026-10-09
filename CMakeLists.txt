cmake_minimum_required(VERSION 3.22)
project(CALevelGen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Dependencies)


#Core logic: plain C++ with no Emscripten dependencies, so it also builds natively.
add_library(core src/LevelGen.cpp)
target_include_directories(core PUBLIC src)
target_link_libraries(core PUBLIC glm::glm xtensor pcg::pcg)

add_executable(app app/main.cpp src/Interface.cpp)
target_link_libraries(app PRIVATE core dearimgui::dearimgui)
if(NOT EMSCRIPTEN)
    #SDL2main supplies the real WinMain()/main() and calls ours.
    target_link_libraries(app PRIVATE SDL2::SDL2main libglew_static)
endif()


if(EMSCRIPTEN)
    #Produces index.html, index.js and index.wasm in the build folder.
    set_target_properties(app PROPERTIES OUTPUT_NAME index SUFFIX ".html")

    #WebGL 2 only (i.e. OpenGL ES 3). These are linker settings; emcc ignores them when compiling.
    #   (SDL2 comes from the dearimgui target.)
    target_link_options(app PRIVATE "SHELL:-s MIN_WEBGL_VERSION=2" "SHELL:-s MAX_WEBGL_VERSION=2")

    #Let the WASM heap grow past its initial size (16MB) instead of aborting.
    #   Growing replaces the heap's ArrayBuffer, so JS must not hold onto views of it between calls.
    target_link_options(app PRIVATE "SHELL:-s ALLOW_MEMORY_GROWTH=1")

    #The shell file is applied at link time, so relink whenever it changes.
    #   SHELL: keeps the flag and its value together, since CMake de-duplicates options.
    set(shellFile ${PROJECT_SOURCE_DIR}/web/shell.html)
    target_link_options(app PRIVATE "SHELL:--shell-file ${shellFile}")
    set_target_properties(app PROPERTIES LINK_DEPENDS ${shellFile})

    #Build, then serve and open the page: cmake --build --preset serve
    add_custom_target(serve
        COMMAND emrun --port 8080 $<TARGET_FILE:app>
        DEPENDS app
        USES_TERMINAL
        VERBATIM)
endif()
