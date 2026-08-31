cmake_minimum_required(VERSION 3.20)
project(simtower_wasm CXX)

# The ported source is fetched, not vendored - see tools/fetch_upstream.sh and
# the note in the README about why.
set(UPSTREAM "${CMAKE_SOURCE_DIR}/upstream" CACHE PATH "the fetched port")

if(NOT EXISTS "${UPSTREAM}/port/src/original_world.cpp")
    message(FATAL_ERROR
        "No port source in ${UPSTREAM}.  Run tools/fetch_upstream.sh first.")
endif()
file(GLOB PORT_SOURCES "${UPSTREAM}/port/src/original_*.cpp")
file(GLOB SHIM_SOURCES "${CMAKE_SOURCE_DIR}/shim/src/*.cpp")

add_executable(simtower ${SHIM_SOURCES} ${PORT_SOURCES}
               "${CMAKE_SOURCE_DIR}/demo/main.cpp")

target_compile_features(simtower PRIVATE cxx_std_20)
target_compile_definitions(simtower PRIVATE UNICODE _UNICODE
                           WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(simtower PRIVATE
    "${CMAKE_SOURCE_DIR}/shim/include"
    "${CMAKE_SOURCE_DIR}/shim/src"
    "${UPSTREAM}/port/src"
    "${UPSTREAM}/port/generated")

# Nothing of the original game is embedded.  The page asks the player for their
# own SIMTOWER.EXE and win32_ne.cpp reads its resources in the browser, so this
# build output carries no copyrighted material and can be published.
target_link_options(simtower PRIVATE
    "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=134217728
    -sSTACK_SIZE=8388608
    -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8']
    -sEXPORTED_FUNCTIONS=['_main','_simtowerLoadExecutable','_malloc','_free']
    # One file, so a stale page can never pair a cached .js with a fresh .wasm.
    -sSINGLE_FILE=1
    -sASSERTIONS=1)

set_target_properties(simtower PROPERTIES
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/docs")
