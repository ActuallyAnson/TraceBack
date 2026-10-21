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

# Analyzer v2: v1 analyzer + causal-ordering re-ranking (separate files so the frozen v1 sources stay byte-identical).
add_library(tb_analyzer_v2 src/analyzer_v2/rerank.cpp)
target_link_libraries(tb_analyzer_v2 tb_analyzer)
add_executable(tb_analyze2 src/analyzer_v2/main.cpp)
target_link_libraries(tb_analyze2 tb_analyzer_v2)
add_library(tb_analyzer_v3 src/analyzer_v3/rerank.cpp)
target_link_libraries(tb_analyzer_v3 tb_analyzer_v2)
add_executable(tb_analyze3 src/analyzer_v3/main.cpp)
target_link_libraries(tb_analyze3 tb_analyzer_v3)

add_library(tb_analyzer_v4 src/analyzer_v4/rerank.cpp)
target_link_libraries(tb_analyzer_v4 tb_analyzer_v3)
add_executable(tb_analyze4 src/analyzer_v4/main.cpp)
target_link_libraries(tb_analyze4 tb_analyzer_v4)

add_library(tb_analyzer_v5 src/analyzer_v5/rerank.cpp)
target_link_libraries(tb_analyzer_v5 tb_analyzer_v4)
add_executable(tb_analyze5 src/analyzer_v5/main.cpp)
target_link_libraries(tb_analyze5 tb_analyzer_v5)

add_library(tb_analyzer_v6 src/analyzer_v6/rerank.cpp)
target_link_libraries(tb_analyzer_v6 tb_analyzer_v5)
add_executable(tb_analyze6 src/analyzer_v6/main.cpp)
target_link_libraries(tb_analyze6 tb_analyzer_v6)

# LLM reasoner (libcurl client for the Nebius Token Factory endpoint) and raw-telemetry baseline.
find_package(CURL REQUIRED)
add_library(tb_llm src/llm/llm.cpp src/llm/reasoner.cpp)
target_link_libraries(tb_llm tb_analyzer tb_analyzer_v2 tb_analyzer_v3 tb_analyzer_v4 tb_analyzer_v5 tb_analyzer_v6 CURL::libcurl)
add_executable(tb_reason src/llm/main.cpp)
target_link_libraries(tb_reason tb_llm)

# Topology 2 (fan-out with a message broker): separate simulator, used only for the generalization check.
add_library(tb_sim2lib src/sim2/fanout.cpp)
target_link_libraries(tb_sim2lib tb_simlib)
add_executable(tb_sim2 src/sim2/main.cpp)
target_link_libraries(tb_sim2 tb_sim2lib)
add_executable(test_sim2 tests/test_sim2.cpp)
target_link_libraries(test_sim2 tb_sim2lib)
add_test(NAME sim2_basics COMMAND test_sim2)

# Verifier: counterfactual replay. Links the simulator as the replay environment, never the analyzer's rules.
add_library(tb_verify src/verify/verify.cpp)
target_link_libraries(tb_verify tb_analyzer tb_simlib tb_sim2lib)
add_executable(tb_verify_cli src/verify/main.cpp)
set_target_properties(tb_verify_cli PROPERTIES OUTPUT_NAME tb_verify)
target_link_libraries(tb_verify_cli tb_verify)

add_executable(tb_investigate src/report/investigate.cpp)
target_link_libraries(tb_investigate tb_llm tb_verify)

add_executable(test_verify tests/test_verify.cpp)
target_link_libraries(test_verify tb_verify)
add_test(NAME verifier COMMAND test_verify)
add_test(NAME topology2_locked COMMAND tb_sim2 verify --manifest ${CMAKE_SOURCE_DIR}/eval/topology2/manifest.json)

# v2 fresh evaluation sets (eval/v2/PLAN.md): locked before any system ran on them.
add_test(NAME v2_fresh_t1_locked COMMAND tb_sim verify --set v2fresh --manifest ${CMAKE_SOURCE_DIR}/eval/v2/fresh/manifest_t1.json)
add_test(NAME v2_fresh_t2_locked COMMAND tb_sim2 verify --set v2fresh --manifest ${CMAKE_SOURCE_DIR}/eval/v2/fresh/manifest_t2.json)

# v2 override rule: unit test.
add_executable(test_override tests/test_override.cpp)
target_link_libraries(test_override tb_llm)
add_test(NAME override_rule COMMAND test_override)

# Hybrid (eval/real3/PLAN.md): LLM-proposed candidates checked and capped by the deterministic layer. New files only.
add_library(tb_hybrid src/hybrid/hybrid.cpp)
target_link_libraries(tb_hybrid tb_llm)
add_executable(tb_hybrid_cli src/hybrid/main.cpp)
set_target_properties(tb_hybrid_cli PROPERTIES OUTPUT_NAME tb_hybrid)
target_link_libraries(tb_hybrid_cli tb_hybrid)
add_executable(test_hybrid tests/test_hybrid.cpp)
target_link_libraries(test_hybrid tb_hybrid)
add_test(NAME hybrid_rules COMMAND test_hybrid)

# Batch-4 fixes (eval/real4/PLAN.md): converter v3 linkage and analyzer v4 rule on synthetic data. Run from the repository's own build directory.
add_test(NAME real4_synthetic COMMAND python3 ${CMAKE_SOURCE_DIR}/eval/real4/synthetic_check.py)

# Hybrid 2 (eval/transfer/PLAN.md): model proposals kept only where the incident's measurements support them. New files only.
add_library(tb_hybrid2 src/hybrid2/hybrid2.cpp)
target_link_libraries(tb_hybrid2 tb_hybrid)
add_executable(tb_hybrid2_cli src/hybrid2/main.cpp)
set_target_properties(tb_hybrid2_cli PROPERTIES OUTPUT_NAME tb_hybrid2)
target_link_libraries(tb_hybrid2_cli tb_hybrid2)
add_executable(tb_hybrid2m src/hybrid2/main_model.cpp)
target_link_libraries(tb_hybrid2m tb_hybrid2)
