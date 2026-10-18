cmake_minimum_required(VERSION 3.16)
project(traceback CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()
# Targets are added per phase (demo services, analyzer, tests).
