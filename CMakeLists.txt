cmake_minimum_required(VERSION 3.20)
project(KeplerBridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure 64-bit architecture
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "KeplerBridge strictly requires a 64-bit (x64) target architecture! 32-bit builds are unsupported.")
endif()

set(SOURCES
    src/dllmain.cpp
    src/logger.cpp
    src/config.cpp
    src/proxy_loader.cpp
    src/feature_level_compat.cpp
)

set(DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/exports.def")

# Create the shared library target named 'd3d12' so the output binary is d3d12.dll
add_library(d3d12 SHARED ${SOURCES} ${DEF_FILE})

target_include_directories(d3d12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Output name strictly 'd3d12.dll' across all compilers (avoid 'libd3d12.dll' on MinGW)
set_target_properties(d3d12 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "d3d12"
)

# Link against standard Windows libraries: dxgi, dxguid, user32
target_link_libraries(d3d12 PRIVATE
    dxgi
    dxguid
    user32
)

# Configure module definition file and ensure standalone static runtime
if(MSVC)
    target_link_options(d3d12 PRIVATE "/DEF:${DEF_FILE}")
else()
    target_link_options(d3d12 PRIVATE
        "${DEF_FILE}"
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

# Post-build command: copy kepler_bridge.ini alongside d3d12.dll
add_custom_command(TARGET d3d12 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/kepler_bridge.ini"
        "$<TARGET_FILE_DIR:d3d12>/kepler_bridge.ini"
    COMMENT "Deploying kepler_bridge.ini to output directory"
)
