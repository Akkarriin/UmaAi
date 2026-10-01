cmake_minimum_required(VERSION 3.16)
project(UmaAi CXX)

# Linux 构建（GCC/Clang）。Windows 仍可使用 UmaSimulator.sln。
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
# 产物：
#   build/UmaAi          主程序（UMAAI_MAINAI），在仓库根目录运行（读取 ./db、./aiConfig_cpu.json、./thisTurn.json）
#   build/UmaAiTestScore 自对局测分（UMAAI_TESTSCORE），在 build/ 目录运行（读取 ../db、../ConfigTemplate/testConfig.json）
#   build/UmaAiTestMujinto 无人岛规则的数值自检（UMAAI_TESTMUJINTO），在 build/ 目录运行
#   build/UmaAiTuneHandwritten 手写策略自动调参（UMAAI_TUNEHW），在 build/ 目录运行，说明见 Tests/tuneHandwritten.cpp

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(OpenMP)
find_package(ZLIB REQUIRED)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/UmaSimulator)
file(GLOB UMAAI_SOURCES
  ${SRC_DIR}/External/cnpy/cnpy.cpp
  ${SRC_DIR}/External/mathFunctions.cpp
  ${SRC_DIR}/External/utils.cpp
  ${SRC_DIR}/Game/*.cpp
  ${SRC_DIR}/GameDatabase/*.cpp
  ${SRC_DIR}/MainCommands/*.cpp
  ${SRC_DIR}/NeuralNet/*.cpp
  ${SRC_DIR}/Search/*.cpp
  ${SRC_DIR}/Selfplay/*.cpp
  ${SRC_DIR}/SupportCard/*.cpp
  ${SRC_DIR}/Tests/*.cpp
  ${SRC_DIR}/UmaSimulator.cpp
)
# 这两个测试用了Windows API，Linux下不编译
list(FILTER UMAAI_SOURCES EXCLUDE REGEX "Tests/(testDemoGame|test_json)\\.cpp$")

# websocket 需要 asio（Ubuntu/Debian: sudo apt install libasio-dev），找不到时只支持 localfile/urafile 模式
find_path(ASIO_INCLUDE_DIR asio.hpp)
if(ASIO_INCLUDE_DIR)
  message(STATUS "UmaAi: websocket enabled (asio: ${ASIO_INCLUDE_DIR})")
  list(APPEND UMAAI_SOURCES ${SRC_DIR}/websocket.cpp)
  set(UMAAI_WS_DEFS ASIO_STANDALONE _WEBSOCKETPP_CPP11_STL_)
  set(UMAAI_WS_INCLUDES ${ASIO_INCLUDE_DIR} ${SRC_DIR}/External)
else()
  message(STATUS "UmaAi: asio not found, websocket disabled")
  set(UMAAI_WS_DEFS UMAAI_NO_WEBSOCKET)
  set(UMAAI_WS_INCLUDES)
endif()

# 只有入口和GamePrint.cpp（PRINT_GAME_EVENT）跟模式有关，其余源文件只编译一次
set(UMAAI_MODE_SOURCES ${SRC_DIR}/UmaSimulator.cpp ${SRC_DIR}/Game/GamePrint.cpp)
list(REMOVE_ITEM UMAAI_SOURCES ${UMAAI_MODE_SOURCES})

function(umaai_setup target)
  target_compile_definitions(${target} PRIVATE UMAAI_MODE_SET_BY_BUILD ${UMAAI_WS_DEFS})
  target_include_directories(${target} PRIVATE ${UMAAI_WS_INCLUDES})
  if(OpenMP_CXX_FOUND)
    target_link_libraries(${target} PRIVATE OpenMP::OpenMP_CXX)
  endif()
  target_compile_options(${target} PRIVATE -w)
endfunction()

add_library(umaai_common OBJECT ${UMAAI_SOURCES})
umaai_setup(umaai_common)

function(umaai_add_target name mode)
  add_executable(${name} ${UMAAI_MODE_SOURCES} $<TARGET_OBJECTS:umaai_common>)
  umaai_setup(${name})
  target_compile_definitions(${name} PRIVATE ${mode})
  target_link_libraries(${name} PRIVATE Threads::Threads ZLIB::ZLIB)
endfunction()

umaai_add_target(UmaAi UMAAI_MAINAI)
umaai_add_target(UmaAiTestScore UMAAI_TESTSCORE)
umaai_add_target(UmaAiTestMujinto UMAAI_TESTMUJINTO)
umaai_add_target(UmaAiTuneHandwritten UMAAI_TUNEHW)
