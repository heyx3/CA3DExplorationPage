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
target_link_libraries(app PRIVATE core)


if(EMSCRIPTEN)
    #Produces index.html, index.js and index.wasm in the build folder.
    set_target_properties(app PROPERTIES OUTPUT_NAME index SUFFIX ".html")

    #The shell file is applied at link time,
    #   so relink whenever it changes.
    set(shellFile ${CMAKE_SOURCE_DIR}/web/shell.html)
    target_link_options(app PRIVATE --shell-file ${shellFile})
    set_target_properties(app PROPERTIES LINK_DEPENDS ${shellFile})
endif()
