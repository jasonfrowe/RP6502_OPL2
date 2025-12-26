cmake_minimum_required(VERSION 3.18)

add_subdirectory(tools)

set(LLVM_MOS_PLATFORM rp6502)
find_package(llvm-mos-sdk REQUIRED)

project(MY-RP6502-PROJECT C CXX ASM)

add_executable(RP6502_OPL2)
rp6502_asset(RP6502_OPL2 0x10000 src/doom.bin)
rp6502_executable(RP6502_OPL2
    doom.bin.rp6502
    DATA file
    RESET file
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.hlp
)
target_sources(RP6502_OPL2 PRIVATE
    src/main.c
    src/opl.c
    src/instruments.c
)
