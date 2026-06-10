cmake_minimum_required(VERSION 3.16)
project(arena_wasm)

set(CMAKE_CXX_STANDARD 17)

add_executable(arena_wasm ../src/wasm_core.cpp)

target_compile_options(arena_wasm PRIVATE -O3 -flto)

target_link_options(arena_wasm PRIVATE
    "-O3"
    "-flto"
    "-lembind"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sENVIRONMENT=web"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=16MB"
    "-sWASM_BIGINT=1"
    "-sMALLOC=emmalloc"
    "-sFILESYSTEM=0"
    "-sASSERTIONS=0"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['wasmMemory']"
)

set_target_properties(arena_wasm PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
