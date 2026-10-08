cmake_minimum_required(VERSION 3.22)
project(CALevelGen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Dependencies)


#Core logic: plain C++ with no Emscripten dependencies, so it also builds natively.
add_library(core src/LevelGen.cpp)
target_include_directories(core PUBLIC src)
target_link_libraries(core PUBLIC glm::glm xtensor pcg::pcg)

add_executable(app app/main.cpp)
target_link_libraries(app PRIVATE core dearimgui::dearimgui)


if(EMSCRIPTEN)
    #Produces index.html, index.js and index.wasm in the build folder.
    set_target_properties(app PROPERTIES OUTPUT_NAME index SUFFIX ".html")

    #Configure Dear ImGUI backends for Emscripten.
    set(EMSCRIPTEN_WEB_FLAGS "SHELL:-s USE_SDL=2" "SHELL:-s USE_WEBGL2=1" "SHELL:-s MIN_WEBGL_VERSION=2")
    target_compile_options(dearimgui PRIVATE ${EMSCRIPTEN_WEB_FLAGS})

    #The shell file is applied at link time, so relink whenever it changes.
    set(shellFile ${CMAKE_SOURCE_DIR}/web/shell.html)
    target_compile_options(app PRIVATE ${EMSCRIPTEN_WEB_FLAGS})
    target_link_options(app PRIVATE --shell-file ${shellFile} ${EMSCRIPTEN_WEB_FLAGS})
    set_target_properties(app PROPERTIES LINK_DEPENDS ${shellFile})

    #Build, then serve and open the page: cmake --build --preset serve
    add_custom_target(serve
        COMMAND emrun --port 8080 $<TARGET_FILE:app>
        DEPENDS app
        USES_TERMINAL
        VERBATIM)
endif()
