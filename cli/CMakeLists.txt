# VGM Studio CLI —— 独立工程
#
# 只依赖 libvgm（已编出的三个 .a）与 nlohmann/json。
# 不与 0x808 共用源码树、不链它的 engine/gui（RULES.md F6/F7/F8）。
#
# 构建：
#   cmake -S cli -B cli/build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   ninja -C cli/build

cmake_minimum_required(VERSION 3.20)
project(vgmstudio_cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ── libvgm ────────────────────────────────────────────────────────────────────
set(VGMSTUDIO_LIBVGM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libvgm/build")
if(NOT EXISTS "${VGMSTUDIO_LIBVGM_DIR}/libvgmConfig.cmake")
    message(FATAL_ERROR
        "libvgm 还没编译。先跑：\n"
        "  cmake -S libvgm -B libvgm/build -G Ninja -DCMAKE_BUILD_TYPE=Release "
        "-DBUILD_LIBAUDIO=OFF -DBUILD_PLAYER=OFF -DBUILD_VGM2WAV=OFF -DUSE_SANITIZERS=OFF")
endif()
find_package(libvgm CONFIG REQUIRED PATHS "${VGMSTUDIO_LIBVGM_DIR}" NO_DEFAULT_PATH)

# ── nlohmann/json ─────────────────────────────────────────────────────────────
find_package(nlohmann_json CONFIG REQUIRED)

# ── 共用库 ────────────────────────────────────────────────────────────────────
add_library(vgmcore STATIC
    src/RegStream.h
    src/AnalyzerYm2151.h
    src/NoteSplitter.h
    src/NoteSplitter.cpp
    src/Document.h
    src/Document.cpp
    src/VgmImportPlayer.h
    src/VgmImportPlayer.cpp
    src/Importer.h
    src/Importer.cpp
    src/VgmRebuild.h
    src/VgmRebuild.cpp
    src/FmVoice.h
    src/FmVoice.cpp
    src/Opm2Opll.h
    src/Opm2Opll.cpp
    src/PreviewVgm.h
    src/PreviewVgm.cpp
)
target_include_directories(vgmcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vgmcore PUBLIC libvgm::vgm-player nlohmann_json::nlohmann_json)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vgmcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ── MIDI 库（直接复制自 Denjhang_Music_Player_v16/midifile，Craig Sapp smf）────
add_library(midifile STATIC
    third_party/midifile/src/Binasc.cpp
    third_party/midifile/src/MidiEvent.cpp
    third_party/midifile/src/MidiEventList.cpp
    third_party/midifile/src/MidiFile.cpp
    third_party/midifile/src/MidiMessage.cpp
    third_party/midifile/src/Options.cpp
)
target_include_directories(midifile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/midifile/include)

# ── 每个业务一个可执行程序 ────────────────────────────────────────────────────
foreach(tool vgminfo vgm2json jsondump json2vgm vgmroundtrip opm2opll)
    add_executable(${tool} src/cli/${tool}.cpp)
    target_link_libraries(${tool} PRIVATE vgmcore)
endforeach()

add_executable(json2mid src/cli/json2mid.cpp)
target_link_libraries(json2mid PRIVATE vgmcore midifile)
