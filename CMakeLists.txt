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
    -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','FS']
    -sEXPORTED_FUNCTIONS=['_main','_simtowerLoadExecutable','_malloc','_free']
    # One file, so a stale page can never pair a cached .js with a fresh .wasm.
    -sSINGLE_FILE=1
    -sASSERTIONS=1)

set_target_properties(simtower PROPERTIES
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/docs")

# The shell is baked into the output at link time but is not a source of any
# target, so nothing rebuilds when it changes: the page keeps its old text and
# the build says there was no work to do.
set_property(TARGET simtower APPEND PROPERTY
             LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html")


# A second build for the command line, so the renderer can be checked without a
# browser.  tools/node_harness.js gives it a fake canvas and writes what was
# presented to a PNG - the same discipline as proof-reading the font offline
# rather than looking at it in a tab.
add_executable(simtower_node ${SHIM_SOURCES} ${PORT_SOURCES}
               "${CMAKE_SOURCE_DIR}/demo/main.cpp")
target_compile_features(simtower_node PRIVATE cxx_std_20)
target_compile_definitions(simtower_node PRIVATE UNICODE _UNICODE
                           WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(simtower_node PRIVATE
    "${CMAKE_SOURCE_DIR}/shim/include"
    "${CMAKE_SOURCE_DIR}/shim/src"
    "${UPSTREAM}/port/src"
    "${UPSTREAM}/port/generated")
target_link_options(simtower_node PRIVATE
    -sENVIRONMENT=node
    -sMODULARIZE=1
    -sEXPORT_NAME=createSimTower
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=134217728
    -sSTACK_SIZE=8388608
    -sEXPORTED_RUNTIME_METHODS=['HEAPU8','callMain','FS']
    -sEXPORTED_FUNCTIONS=['_main','_simtowerLoadExecutable','_malloc','_free']
    -sINVOKE_RUN=0
    -sASSERTIONS=1)
set_target_properties(simtower_node PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/node")


# The game itself: the port's own Win32 host, with Asyncify so its PeekMessage
# loop can yield to the browser instead of freezing the tab.
add_executable(simtower_game ${SHIM_SOURCES} ${PORT_SOURCES}
               "${UPSTREAM}/port/src/native_main.cpp"
               "${CMAKE_SOURCE_DIR}/game/entry.cpp")
target_compile_features(simtower_game PRIVATE cxx_std_20)
target_compile_definitions(simtower_game PRIVATE UNICODE _UNICODE
                           WIN32_LEAN_AND_MEAN NOMINMAX)
# WinMain wraps its whole body in try/catch, so catching has to be enabled;
# without it the first throw aborts with an assertion instead.
target_compile_options(simtower_game PRIVATE -fexceptions)
target_include_directories(simtower_game PRIVATE
    "${CMAKE_SOURCE_DIR}/shim/include"
    "${CMAKE_SOURCE_DIR}/shim/src"
    "${UPSTREAM}/port/src"
    "${UPSTREAM}/port/generated")
target_link_options(simtower_game PRIVATE
    "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
    -fexceptions
    -sASYNCIFY=1
    -sASYNCIFY_STACK_SIZE=131072
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=268435456
    -sSTACK_SIZE=16777216
    -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','FS']
    -sEXPORTED_FUNCTIONS=['_main','_simtowerLoadExecutable','_malloc','_free']
    -sSINGLE_FILE=1
    # Saved towers live in an IndexedDB-backed directory so they survive a
    # reload.  Without this the runtime keeps a stub that answers "IDBFS is
    # no longer included by default" and the mount quietly fails.
    -lidbfs.js
    -sASSERTIONS=1)
set_target_properties(simtower_game PROPERTIES
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/docs")
set_property(TARGET simtower_game APPEND PROPERTY
             LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html")

# The same thing for the command line, so it can be watched without a browser.
add_executable(simtower_game_node ${SHIM_SOURCES} ${PORT_SOURCES}
               "${UPSTREAM}/port/src/native_main.cpp"
               "${CMAKE_SOURCE_DIR}/game/entry.cpp")
target_compile_features(simtower_game_node PRIVATE cxx_std_20)
target_compile_definitions(simtower_game_node PRIVATE UNICODE _UNICODE
                           WIN32_LEAN_AND_MEAN NOMINMAX)
# WinMain wraps its whole body in try/catch, so catching has to be enabled;
# without it the first throw aborts with an assertion instead.
target_compile_options(simtower_game_node PRIVATE -fexceptions)
target_include_directories(simtower_game_node PRIVATE
    "${CMAKE_SOURCE_DIR}/shim/include"
    "${CMAKE_SOURCE_DIR}/shim/src"
    "${UPSTREAM}/port/src"
    "${UPSTREAM}/port/generated")
target_link_options(simtower_game_node PRIVATE
    -sENVIRONMENT=node
    -sMODULARIZE=1
    -sEXPORT_NAME=createSimTower
    -fexceptions
    -sASYNCIFY=1
    -sASYNCIFY_STACK_SIZE=131072
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=268435456
    -sSTACK_SIZE=16777216
    -sEXPORTED_RUNTIME_METHODS=['HEAPU8','callMain','FS']
    -sEXPORTED_FUNCTIONS=['_main','_simtowerLoadExecutable','_malloc','_free']
    -sINVOKE_RUN=0
    -lidbfs.js
    -sASSERTIONS=1)
set_target_properties(simtower_game_node PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/node")
