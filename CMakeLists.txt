cmake_minimum_required(VERSION 3.15)
project(myCPU LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES
    src/main.cpp
    src/cpu/CPU.cpp
    src/cpu/Decoder.cpp
    src/cpu/RegisterFile.cpp
    src/cpu/CsrFile.cpp
    src/cpu/TrapHandler.cpp
    src/memory/Memory.cpp
    src/bus/Bus.cpp
    src/devices/Device.cpp
    src/assembler/Assembler.cpp
    src/rpc/RpcServer.cpp
    src/elf/ElfLoader.cpp
)

add_executable(myCPU ${SOURCES})

if (WIN32)
    target_link_libraries(myCPU ws2_32)
endif()
