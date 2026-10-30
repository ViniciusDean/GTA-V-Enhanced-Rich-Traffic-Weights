cmake_minimum_required(VERSION 3.20)
project(RichTrafficWeights VERSION 1.4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SCRIPHOOK_SDK "" CACHE PATH "Path to the official ScriptHookV SDK root")

if(NOT EXISTS "${SCRIPHOOK_SDK}/inc/main.h" OR NOT EXISTS "${SCRIPHOOK_SDK}/lib/ScriptHookV.lib")
    message(FATAL_ERROR "Set SCRIPHOOK_SDK to the official ScriptHookV SDK root")
endif()

add_library(RichTrafficWeights SHARED
    src/main.cpp
    src/RichTrafficWeights.cpp
    src/Policy.cpp
)
target_include_directories(RichTrafficWeights PRIVATE
    "${SCRIPHOOK_SDK}/inc"
    "${CMAKE_CURRENT_LIST_DIR}/src"
)
target_link_libraries(RichTrafficWeights PRIVATE "${SCRIPHOOK_SDK}/lib/ScriptHookV.lib")
target_compile_definitions(RichTrafficWeights PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(RichTrafficWeights PRIVATE /W4 /EHsc /wd4505)
set_property(TARGET RichTrafficWeights PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_target_properties(RichTrafficWeights PROPERTIES PREFIX "" SUFFIX ".asi")

add_executable(RichTrafficWeightsPolicyTests
    tests/PolicyTests.cpp
    src/Policy.cpp
)
target_include_directories(RichTrafficWeightsPolicyTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
target_compile_options(RichTrafficWeightsPolicyTests PRIVATE /W4 /EHsc)
set_property(TARGET RichTrafficWeightsPolicyTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

enable_testing()
add_test(NAME RichTrafficWeightsPolicyTests COMMAND RichTrafficWeightsPolicyTests)
