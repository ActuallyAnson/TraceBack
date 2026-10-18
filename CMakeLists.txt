cmake_minimum_required(VERSION 3.16)
project(traceback CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)
include_directories(include)
enable_testing()

# Simulator: emits logs/spans/metrics. Deliberately shares no code with the analyzer.
add_library(tb_simlib src/sim/sim.cpp src/sim/catalog.cpp)
add_executable(tb_sim src/sim/main.cpp)
target_link_libraries(tb_sim tb_simlib)

add_executable(test_sim tests/test_sim.cpp)
target_link_libraries(test_sim tb_simlib)
add_test(NAME sim_determinism COMMAND test_sim)
add_test(NAME split_locked COMMAND tb_sim verify --manifest ${CMAKE_SOURCE_DIR}/eval/split.json)

# Analyzer: reads observable incident files only. Does not link the simulator.
add_library(tb_analyzer src/analyzer/load.cpp src/analyzer/correlate.cpp src/analyzer/hypotheses.cpp)
add_executable(tb_analyze src/analyzer/main.cpp)
target_link_libraries(tb_analyze tb_analyzer)

add_executable(test_analyzer tests/test_analyzer.cpp)
target_link_libraries(test_analyzer tb_analyzer)
add_test(NAME analyzer_units COMMAND test_analyzer)

# LLM reasoner (libcurl client for the Nebius Token Factory endpoint) and raw-telemetry baseline.
find_package(CURL REQUIRED)
add_library(tb_llm src/llm/llm.cpp src/llm/reasoner.cpp)
target_link_libraries(tb_llm tb_analyzer CURL::libcurl)
add_executable(tb_reason src/llm/main.cpp)
target_link_libraries(tb_reason tb_llm)
