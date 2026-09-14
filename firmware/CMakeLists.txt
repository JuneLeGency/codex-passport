cmake_minimum_required(VERSION 3.16)
# Hardware dependency; application code is authored independently in main/.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../upstream/ai-passport/components/bsp")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.3.1")
project(codex-passport)
