cmake_minimum_required(VERSION 3.20)
project(KeplerBridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure 64-bit architecture
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "KeplerBridge strictly requires a 64-bit (x64) target architecture! 32-bit builds are unsupported.")
endif()

set(CORE_SOURCES
    src/logger.cpp
    src/config.cpp
    src/proxy_loader.cpp
    src/feature_level_compat.cpp
    src/dxil_inspector.cpp
    src/shader_policy.cpp
    src/texture_clamper.cpp
    src/pso_hook.cpp
    src/command_translator.cpp
    src/resource_manager.cpp
    src/d3d11_backend.cpp
    src/wrapped_command_objects.cpp
    src/wrapped_dxgi_swapchain.cpp
    src/bounded_shader_translator.cpp
    src/dxbc_checksum.cpp
    src/sm51_lowering.cpp
    src/dxil_translation.cpp
    src/shader_disk_cache.cpp
    src/profile_manager.cpp
    src/wrapped_d3d12_device_factory.cpp
    src/capability_registry.cpp
    src/shader_autopsy.cpp
    src/memory_guardian.cpp
    src/adaptive_engine.cpp
    src/fsr1_upscaler.cpp
    src/descriptor_virtualizer.cpp
    src/wave_emulation.cpp
    src/int64_lowering.cpp
    src/mesh_shader_compat.cpp
    src/unified_feature_policy.cpp
    src/dxil_bitcode_decoder.cpp
    src/dxil_mem2reg.cpp
)

set(SOURCES
    src/dllmain.cpp
    ${CORE_SOURCES}
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

# Link against standard Windows libraries: dxgi, dxguid, user32, d3d11, d3dcompiler, crypt32
target_link_libraries(d3d12 PRIVATE
    dxgi
    dxguid
    user32
    d3d11
    d3dcompiler
    crypt32
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

# --- Test Targets ---

# Unit Test: Policy Engine
add_executable(test_policy_engine
    tests/test_policy_engine.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_policy_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_policy_engine PRIVATE dxgi dxguid user32 d3d11 d3dcompiler crypt32)
if(NOT MSVC)
    target_link_options(test_policy_engine PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Unit Test: GameProfileManager & Capability Policy (Phase 11B)
add_executable(test_profile_manager
    tests/test_profile_manager.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_profile_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_profile_manager PRIVATE dxgi dxguid user32 d3d11 d3dcompiler crypt32)
if(NOT MSVC)
    target_link_options(test_profile_manager PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Unit Test: Shader & Resource Inspector
add_executable(test_shader_inspector
    tests/test_shader_inspector.cpp
    src/dxil_inspector.cpp
    src/shader_policy.cpp
    src/texture_clamper.cpp
    src/config.cpp
    src/profile_manager.cpp
    src/logger.cpp
)
target_include_directories(test_shader_inspector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_shader_inspector PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_shader_inspector PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Unit Test: Phase 4 Translation Engine
add_executable(test_translation_engine
    tests/test_translation_engine.cpp
    src/command_translator.cpp
    src/resource_manager.cpp
    src/d3d11_backend.cpp
    src/bounded_shader_translator.cpp
    src/dxbc_checksum.cpp
    src/sm51_lowering.cpp
    src/dxil_translation.cpp
    src/dxil_bitcode_decoder.cpp
    src/dxil_mem2reg.cpp
    src/shader_disk_cache.cpp
    src/dxil_inspector.cpp
    src/shader_policy.cpp
    src/config.cpp
    src/profile_manager.cpp
    src/logger.cpp
)
target_include_directories(test_translation_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_translation_engine PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_translation_engine PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Integration Test: Live Hardware D3D12 Feature Level Compatibility
add_executable(test_d3d12_compat
    tests/test_d3d12_compat.cpp
)
target_include_directories(test_d3d12_compat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_compat PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_d3d12_compat PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Integration Test: Live Hardware PSO & Resource Interception
add_executable(test_pso_interception
    tests/test_pso_interception.cpp
)
target_include_directories(test_pso_interception PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_pso_interception PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_pso_interception PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Integration Test: Live Hardware Phase 4 Translation & Execution
add_executable(test_d3d12_translation
    tests/test_d3d12_translation.cpp
)
target_include_directories(test_d3d12_translation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_translation PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_d3d12_translation PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Integration Test: Live Hardware Phase 4 Rendering & Presentation Proof
add_executable(test_d3d12_rendering
    tests/test_d3d12_rendering.cpp
)
target_include_directories(test_d3d12_rendering PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_rendering PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_d3d12_rendering PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Integration Test: End-to-End D3D12 Integration Harness (Workloads A through E)
add_executable(test_d3d12_e2e_integration
    tests/test_d3d12_e2e_integration.cpp
)
target_include_directories(test_d3d12_e2e_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_e2e_integration PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_d3d12_e2e_integration PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Test B: Standalone D3D11 Initialization
add_executable(test_d3d11_standalone
    tests/test_d3d11_standalone.cpp
)
target_include_directories(test_d3d11_standalone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d11_standalone PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_d3d11_standalone PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Integration Test: Live Hardware Phase 5 Application DXGI SwapChain
add_executable(test_dxgi_swapchain
    tests/test_dxgi_swapchain.cpp
)
target_include_directories(test_dxgi_swapchain PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_dxgi_swapchain PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_dxgi_swapchain PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Integration Test: Live Hardware Phase 6.1 Compute Shader Execution & UAV Readback
add_executable(test_d3d12_compute
    tests/test_d3d12_compute.cpp
)
target_include_directories(test_d3d12_compute PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_compute PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_d3d12_compute PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Integration Test: Live Hardware Phase 7 Multi-Slot UAV & Readback Proof
add_executable(test_d3d12_multi_uav
    tests/test_d3d12_multi_uav.cpp
)
target_include_directories(test_d3d12_multi_uav PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_multi_uav PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_d3d12_multi_uav PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Unit & Integration Test: Phase 6 Bounded Shader Translator & Compatibility
add_executable(test_shader_compat
    tests/test_shader_compat.cpp
    src/bounded_shader_translator.cpp
    src/dxbc_checksum.cpp
    src/sm51_lowering.cpp
    src/dxil_translation.cpp
    src/dxil_bitcode_decoder.cpp
    src/dxil_mem2reg.cpp
    src/shader_disk_cache.cpp
    src/dxil_inspector.cpp
    src/shader_policy.cpp
    src/config.cpp
    src/profile_manager.cpp
    src/logger.cpp
)
target_include_directories(test_shader_compat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_shader_compat PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_shader_compat PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Integration Test: Phase 7 Shader Cache Multi-Process Persistence & Invalidation
add_executable(test_shader_cache_persistence
    tests/test_shader_cache_persistence.cpp
    src/bounded_shader_translator.cpp
    src/dxbc_checksum.cpp
    src/sm51_lowering.cpp
    src/dxil_translation.cpp
    src/dxil_bitcode_decoder.cpp
    src/dxil_mem2reg.cpp
    src/shader_disk_cache.cpp
    src/dxil_inspector.cpp
    src/shader_policy.cpp
    src/config.cpp
    src/profile_manager.cpp
    src/logger.cpp
)
target_include_directories(test_shader_cache_persistence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_shader_cache_persistence PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_shader_cache_persistence PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Diagnostic Tool: Agility SDK & System32 DXGI Probing
add_executable(test_agility_diag
    tests/test_agility_diag.cpp
)
target_include_directories(test_agility_diag PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_agility_diag PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_agility_diag PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Unit & Integration Test: Phase 11D Device Factory Interception
add_executable(test_device_factory
    tests/test_device_factory.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_device_factory PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_device_factory PRIVATE dxgi dxguid user32 d3d11 d3dcompiler crypt32)
if(NOT MSVC)
    target_link_options(test_device_factory PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Automated ABI Test: Phase 11E D3D12 Export Parity & Dependent Module Import Test
add_executable(test_export_parity
    tests/test_export_parity.cpp
)
target_include_directories(test_export_parity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_export_parity PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_export_parity PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Minimal Controlled Smoke Test Harness (Stage 3 & 4)
add_executable(d3d12_smoke_test
    test_results/D3D12SmokeTest/d3d12_smoke_test.cpp
)
target_include_directories(d3d12_smoke_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(d3d12_smoke_test PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(d3d12_smoke_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Performance Profiling & Optimization Benchmark Test
add_executable(test_perf_profile
    tests/test_perf_profile.cpp
    src/bounded_shader_translator.cpp
    src/dxbc_checksum.cpp
    src/sm51_lowering.cpp
    src/dxil_translation.cpp
    src/dxil_bitcode_decoder.cpp
    src/dxil_mem2reg.cpp
    src/shader_disk_cache.cpp
    src/dxil_inspector.cpp
    src/shader_policy.cpp
    src/command_translator.cpp
    src/resource_manager.cpp
    src/d3d11_backend.cpp
    src/config.cpp
    src/profile_manager.cpp
    src/logger.cpp
)
target_include_directories(test_perf_profile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_perf_profile PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_perf_profile PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Legacy Phase 1 Test
add_executable(test_d3d12
    tests/test_d3d12.cpp
)
target_include_directories(test_d3d12 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12 PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_d3d12 PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Phase 11F.1 Device Interfaces Test
add_executable(test_device_interfaces
    tests/test_device_interfaces.cpp
)
target_include_directories(test_device_interfaces PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_device_interfaces PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_device_interfaces PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Phase 11H SM 5.1 Lowering Test
add_executable(test_sm51_lowering
    tests/test_sm51_lowering.cpp
    src/sm51_lowering.cpp
    src/dxbc_checksum.cpp
    src/dxil_inspector.cpp
    src/bounded_shader_translator.cpp
    src/dxil_translation.cpp
    src/dxil_bitcode_decoder.cpp
    src/dxil_mem2reg.cpp
    src/shader_disk_cache.cpp
    src/config.cpp
    src/profile_manager.cpp
    src/logger.cpp
)
target_include_directories(test_sm51_lowering PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_sm51_lowering PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_sm51_lowering PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Phase 11I SM 5.1 Graphics Rendering & Pixel Readback Test
add_executable(test_d3d12_rendering_sm51
    tests/test_d3d12_rendering_sm51.cpp
)
target_include_directories(test_d3d12_rendering_sm51 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_rendering_sm51 PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_d3d12_rendering_sm51 PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Phase 11I SM 5.1 Compute Dispatch & Math Readback Test
add_executable(test_d3d12_compute_sm51
    tests/test_d3d12_compute_sm51.cpp
)
target_include_directories(test_d3d12_compute_sm51 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_compute_sm51 PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_d3d12_compute_sm51 PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Phase 11J Device-Removal Handling Test
add_executable(test_d3d12_device_removed_handling
    tests/test_d3d12_device_removed_handling.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_d3d12_device_removed_handling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_device_removed_handling PRIVATE dxgi dxguid user32 d3d11 d3dcompiler crypt32)
if(NOT MSVC)
    target_link_options(test_d3d12_device_removed_handling PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Phase 11J D3D11 UAV/SRV Hazard Regression Test
add_executable(test_d3d12_uav_hazard
    tests/test_d3d12_uav_hazard.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_d3d12_uav_hazard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_d3d12_uav_hazard PRIVATE dxgi dxguid user32 d3d11 d3dcompiler crypt32)
if(NOT MSVC)
    target_link_options(test_d3d12_uav_hazard PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Phase 12 Real DXIL-to-D3D11 Shader Translation Suite
add_executable(test_dxil_translation
    tests/test_dxil_translation.cpp
    src/dxil_translation.cpp
    src/dxil_bitcode_decoder.cpp
    src/dxil_mem2reg.cpp
    src/dxbc_checksum.cpp
    src/dxil_inspector.cpp
    src/bounded_shader_translator.cpp
    src/sm51_lowering.cpp
    src/shader_disk_cache.cpp
    src/shader_autopsy.cpp
    src/shader_policy.cpp
    src/config.cpp
    src/capability_registry.cpp
    src/profile_manager.cpp
    src/logger.cpp
)
target_include_directories(test_dxil_translation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_dxil_translation PRIVATE dxgi dxguid user32 d3d11 d3dcompiler crypt32)
if(NOT MSVC)
    target_link_options(test_dxil_translation PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 1: Capability Registry Test
add_executable(test_capability_registry
    tests/test_capability_registry.cpp
    src/capability_registry.cpp
    src/logger.cpp
)
target_include_directories(test_capability_registry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_capability_registry PRIVATE dxgi dxguid user32 d3d11)
if(NOT MSVC)
    target_link_options(test_capability_registry PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 3: Shader Autopsy Test
add_executable(test_shader_autopsy
    tests/test_shader_autopsy.cpp
    src/shader_autopsy.cpp
    src/dxil_translation.cpp
    src/dxil_bitcode_decoder.cpp
    src/dxil_mem2reg.cpp
    src/dxbc_checksum.cpp
    src/logger.cpp
)
target_include_directories(test_shader_autopsy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_shader_autopsy PRIVATE dxgi dxguid user32 crypt32)
if(NOT MSVC)
    target_link_options(test_shader_autopsy PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 4: Shader Cache Quota & Composite Key Test
add_executable(test_shader_cache_quota
    tests/test_shader_cache_quota.cpp
    src/shader_disk_cache.cpp
    src/logger.cpp
)
target_include_directories(test_shader_cache_quota PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_shader_cache_quota PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_shader_cache_quota PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 5: Memory Guardian Test
add_executable(test_memory_guardian
    tests/test_memory_guardian.cpp
    src/memory_guardian.cpp
    src/logger.cpp
)
target_include_directories(test_memory_guardian PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_memory_guardian PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_memory_guardian PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 6: Adaptive Engine Test
add_executable(test_adaptive_engine
    tests/test_adaptive_engine.cpp
    src/adaptive_engine.cpp
    src/logger.cpp
)
target_include_directories(test_adaptive_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_adaptive_engine PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_adaptive_engine PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 7: Profile Schema & Presets Test
add_executable(test_profile_schema
    tests/test_profile_schema.cpp
    src/profile_manager.cpp
    src/config.cpp
    src/logger.cpp
)
target_include_directories(test_profile_schema PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_profile_schema PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_profile_schema PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 8: FSR 1 Upscaler Test
add_executable(test_fsr1_upscaler
    tests/test_fsr1_upscaler.cpp
    src/fsr1_upscaler.cpp
    src/logger.cpp
)
target_include_directories(test_fsr1_upscaler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_fsr1_upscaler PRIVATE dxgi dxguid user32 d3d11 d3dcompiler)
if(NOT MSVC)
    target_link_options(test_fsr1_upscaler PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 9: Descriptor Virtualizer Test
add_executable(test_descriptor_virtualizer
    tests/test_descriptor_virtualizer.cpp
    src/descriptor_virtualizer.cpp
    src/logger.cpp
)
target_include_directories(test_descriptor_virtualizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_descriptor_virtualizer PRIVATE dxgi dxguid user32)
if(NOT MSVC)
    target_link_options(test_descriptor_virtualizer PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 10: Wave Emulation Test
add_executable(test_wave_emulation
    tests/test_wave_emulation.cpp
    src/wave_emulation.cpp
    src/capability_registry.cpp
    src/logger.cpp
)
target_include_directories(test_wave_emulation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_wave_emulation PRIVATE dxgi dxguid user32 d3d11 d3dcompiler)
if(NOT MSVC)
    target_link_options(test_wave_emulation PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 11: 64-Bit Integer Lowering Test
add_executable(test_int64_lowering
    tests/test_int64_lowering.cpp
    src/int64_lowering.cpp
    src/logger.cpp
)
target_include_directories(test_int64_lowering PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_int64_lowering PRIVATE dxgi dxguid user32 d3d11 d3dcompiler)
if(NOT MSVC)
    target_link_options(test_int64_lowering PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 12: Mesh Shader Compatibility Test
add_executable(test_mesh_shader_compat
    tests/test_mesh_shader_compat.cpp
    src/mesh_shader_compat.cpp
    src/logger.cpp
)
target_include_directories(test_mesh_shader_compat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_mesh_shader_compat PRIVATE dxgi dxguid user32 d3d11 d3dcompiler)
if(NOT MSVC)
    target_link_options(test_mesh_shader_compat PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Milestone 13: Unified Feature Policy Test
add_executable(test_unified_policy
    tests/test_unified_policy.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_unified_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_unified_policy PRIVATE dxgi dxguid user32 d3d11 d3dcompiler crypt32)
if(NOT MSVC)
    target_link_options(test_unified_policy PRIVATE -static -static-libgcc -static-libstdc++)
endif()
