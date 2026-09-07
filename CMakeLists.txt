cmake_minimum_required(VERSION 3.26)
project(AnggeumEngine VERSION 1.0.0 LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Vulkan SDK path (from environment variable or manual setting)
if(DEFINED ENV{VULKAN_SDK})
    set(VULKAN_SDK $ENV{VULKAN_SDK})
else()
    # Update this path as needed
    set(VULKAN_SDK "C:/VulkanSDK/1.4.357.0")
endif()

# Verify Vulkan SDK exists
if(NOT EXISTS "${VULKAN_SDK}")
    message(FATAL_ERROR "Vulkan SDK not found at ${VULKAN_SDK}. Please check the path or set VULKAN_SDK environment variable.")
endif()

message(STATUS "Using Vulkan SDK: ${VULKAN_SDK}")

# Find Vulkan package
find_package(Vulkan REQUIRED)

# GLFW setup (using binary version)
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/glfw-3.3.8.bin.WIN64")
set(GLFW_INCLUDE_DIR "${GLFW_DIR}/include")

# 64-bit library path
if(MSVC)
    # Select library matching Visual Studio version
    set(GLFW_LIBRARY "${GLFW_DIR}/lib-vc2022/glfw3.lib")
else()
    set(GLFW_LIBRARY "${GLFW_DIR}/lib-mingw-w64/libglfw3.a")
endif()

# KTX library setup
set(KTX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/ktx")
set(KTX_INCLUDE_DIR "${KTX_DIR}/include")

# Use different KTX libraries for Debug/Release
if(EXISTS "${KTX_DIR}/Debug/ktx.lib")
    set(KTX_LIBRARY_DEBUG "${KTX_DIR}/Debug/ktx.lib")
else()
    set(KTX_LIBRARY_DEBUG "${KTX_DIR}/Release/ktx.lib")
    message(WARNING "KTX Debug library not found, using Release version")
endif()
set(KTX_LIBRARY_RELEASE "${KTX_DIR}/Release/ktx.lib")

# Vulkan SDK library path (Debug/Release)
set(VULKAN_LIB_DIR "${VULKAN_SDK}/Lib")

# Collect source files
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "Core/*.cpp"
    "Core/*.h"
)

file(GLOB_RECURSE SAMPLE_SOURCES CONFIGURE_DEPENDS
    "Sample/*.cpp"
    "Sample/*.h"
)

file(GLOB IMGUI_SOURCES
    "ThirdParties/imgui/*.cpp"
    "ThirdParties/imgui/*.h"
    "ThirdParties/imgui/backends/imgui_impl_glfw.cpp"
    "ThirdParties/imgui/backends/imgui_impl_vulkan.cpp"
)

file(GLOB ROOT_SOURCES
    "main.cpp"
    "stdafx.h"
    "stdafx.cpp"
)

# Collect shader files
file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS
    "Assets/Shaders/*.vert"
    "Assets/Shaders/*.frag"
    "Assets/Shaders/*.comp"
    "Assets/Shaders/*.geom"
    "Assets/Shaders/*.tesc"
    "Assets/Shaders/*.tese"
)

# Collect .glsl header files
file(GLOB_RECURSE SHADER_HEADERS CONFIGURE_DEPENDS
    "Assets/Shaders/*.glsl"
    "Assets/Shaders/*.h"
)

# Display original shader files in IDE (exclude from build)
file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS
    "Assets/Shaders/*.vert"
    "Assets/Shaders/*.frag"
    "Assets/Shaders/*.comp"
    "Assets/Shaders/*.geom"
    "Assets/Shaders/*.tesc"
    "Assets/Shaders/*.tese"
    "Assets/Shaders/*.glsl"
    "Assets/Shaders/*.h"
)

set_source_files_properties(${SHADER_FILES} PROPERTIES
    HEADER_FILE_ONLY TRUE
)

# Create executable (before shader compilation setup)
add_executable(${PROJECT_NAME}
    ${ROOT_SOURCES}
    ${CORE_SOURCES}
    ${SAMPLE_SOURCES}
    ${IMGUI_SOURCES}
    ${SHADER_FILES}
)

# Precompiled Header setup (CMake 3.16+)
target_precompile_headers(${PROJECT_NAME} PRIVATE "stdafx.h")

# Find glslc compiler and setup shader compilation
find_program(GLSLC glslc HINTS
    ${VULKAN_SDK}/Bin
    ${VULKAN_SDK}/Bin32
    $ENV{VULKAN_SDK}/Bin
    $ENV{VULKAN_SDK}/Bin32
)

if(NOT GLSLC)
    message(WARNING "glslc not found - shaders will not be compiled automatically")
else()
    message(STATUS "Found glslc: ${GLSLC}")
    
    # Shader compilation flags
    set(GLSLC_FLAGS
        -g                                    # Generate debug info for RenderDoc/NSight
        --target-env=vulkan1.3
        -I${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders
    )
    
    # Add optimization for Release builds
    if(CMAKE_BUILD_TYPE MATCHES Release)
        list(APPEND GLSLC_FLAGS -O)
    endif()

    # Directory for shader dependency files
    set(SHADER_DEP_DIR "${CMAKE_BINARY_DIR}/shader_deps")
    file(MAKE_DIRECTORY "${SHADER_DEP_DIR}")

    # Create compilation rules for each shader file
    set(COMPILED_SHADERS)
    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        # Keep the source's subdirectory (e.g. Terrain/) in the .spv path.
        file(RELATIVE_PATH SHADER_REL
            "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders" ${SHADER})
        set(SHADER_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders/${SHADER_REL}.spv")
        string(REPLACE "/" "_" SHADER_DEP_NAME "${SHADER_REL}")
        set(SHADER_DEP_FILE "${SHADER_DEP_DIR}/${SHADER_DEP_NAME}.d")

        # -MD writes the #include list at build time; DEPFILE keeps it
        # current without reconfiguring.
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${GLSLC} ${GLSLC_FLAGS}
                -MD -MF ${SHADER_DEP_FILE} -MT ${SHADER_OUTPUT}
                -o ${SHADER_OUTPUT} ${SHADER}
            DEPENDS ${SHADER}
            DEPFILE ${SHADER_DEP_FILE}
            COMMENT "Compiling shader: ${SHADER_NAME}"
            VERBATIM
        )
        
        list(APPEND COMPILED_SHADERS ${SHADER_OUTPUT})
    endforeach()
    
    # Create shader compilation target
    add_custom_target(CompileShaders ALL DEPENDS ${COMPILED_SHADERS})
    
    # Copy compiled .spv files to output directory after shader compilation
    add_custom_command(TARGET CompileShaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/Assets/Shaders"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets/Shaders"
        COMMENT "Syncing compiled shaders to output directory..."
    )

    # Make main target depend on shader compilation
    add_dependencies(${PROJECT_NAME} CompileShaders)
endif()

# Include directory setup
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Sample
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/tinygltf
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/rapidjson/include
    ${KTX_INCLUDE_DIR}
    ${GLFW_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${VULKAN_SDK}/Include
    ${FFX_CACAO_INCLUDE_DIRS}
)

# Link libraries (Debug/Release)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    ${GLFW_LIBRARY}
    debug ${KTX_LIBRARY_DEBUG}
    optimized ${KTX_LIBRARY_RELEASE}
    debug ${VULKAN_LIB_DIR}/shaderc_sharedd.lib
    optimized ${VULKAN_LIB_DIR}/shaderc_shared.lib
    debug ${VULKAN_LIB_DIR}/spirv-cross-cored.lib
    optimized ${VULKAN_LIB_DIR}/spirv-cross-core.lib
    debug ${VULKAN_LIB_DIR}/spirv-cross-glsld.lib
    optimized ${VULKAN_LIB_DIR}/spirv-cross-glsl.lib
    debug ${VULKAN_LIB_DIR}/spirv-cross-reflectd.lib
    optimized ${VULKAN_LIB_DIR}/spirv-cross-reflect.lib
    ffx_cacao_vk
)

# FidelityFX CACAO setup
set(FFX_CACAO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/FidelityFX-CACAO")
if(NOT EXISTS "${FFX_CACAO_DIR}")
    message(FATAL_ERROR "FidelityFX-CACAO submodule not found at ${FFX_CACAO_DIR}. Please initialize the submodule.")
endif()

# Add CACAO subdirectory if it has a CMakeLists.txt
if(EXISTS "${FFX_CACAO_DIR}/CMakeLists.txt")
    add_subdirectory(${FFX_CACAO_DIR} EXCLUDE_FROM_ALL)
else()
    # Manual CACAO library setup if no CMakeLists.txt exists
    set(FFX_CACAO_INCLUDE_DIRS 
        "${FFX_CACAO_DIR}/ffx-cacao"
        "${FFX_CACAO_DIR}/ffx-cacao/inc"
        "${FFX_CACAO_DIR}/ffx-cacao/src"
    )
    
    file(GLOB FFX_CACAO_SOURCES
        "${FFX_CACAO_DIR}/ffx-cacao/src/*.cpp"
        "${FFX_CACAO_DIR}/ffx-cacao/src/*.c"
    )
    
    # Create CACAO static library
    add_library(ffx_cacao_vk STATIC ${FFX_CACAO_SOURCES})
    target_include_directories(ffx_cacao_vk PUBLIC ${FFX_CACAO_INCLUDE_DIRS})
    target_compile_definitions(ffx_cacao_vk PUBLIC FFX_CACAO_ENABLE_VULKAN=1)
    target_link_libraries(ffx_cacao_vk PUBLIC Vulkan::Vulkan)
endif()

message(STATUS "Using FidelityFX CACAO: ${FFX_CACAO_DIR}")

# Compiler-specific settings
if(MSVC)
    # MSVC compiler options
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3
        /MP         # Multi-processor compilation
        /permissive-
    )
    
    # Vulkan-related macro definitions
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        FFX_CACAO_ENABLE_VULKAN=1
    )
    
    # Runtime library settings
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    
    # Debug mode settings
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/RTC1>    # Runtime checks
        $<$<CONFIG:Debug>:/Od>      # Disable optimization
        $<$<CONFIG:Debug>:/Zi>      # Debug info
    )
    
    # Release mode settings
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2>    # Optimization
        $<$<CONFIG:Release>:/Zi>    # Debug info (optional)
        $<$<CONFIG:Release>:/sdl>   # Enable security checks
    )
    
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
    
else()
    # GCC/Clang options
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        # Auto-initialization for GCC/Clang
        $<$<CONFIG:Release>:-ftrivial-auto-var-init=zero>
    )
endif()

# Copy Assets folder (post-build) - only copy changed files, then bake raw
# png/jpg into mipmapped KTX2 siblings (output dir only; the loader prefers
# the baked file, and the upload path has no runtime mip generation).
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Syncing Assets from ${CMAKE_SOURCE_DIR}/Assets to $<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/Assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/Tools/BakeTextures.ps1"
        -AssetsDir "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
        -Toktx "${KTX_DIR}/Release/toktx.exe"
    COMMENT "Syncing Assets folder (changed files only)..."
)

# Copy KTX DLL (Debug/Release, fallback if missing)
set(KTX_DLL_DEBUG "${KTX_DIR}/Debug/ktx.dll")
set(KTX_DLL_RELEASE "${KTX_DIR}/Release/ktx.dll")

if(EXISTS "${KTX_DLL_DEBUG}" AND EXISTS "${KTX_DLL_RELEASE}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${KTX_DLL_DEBUG},${KTX_DLL_RELEASE}>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying ktx.dll ($<CONFIG>)..."
    )
elseif(EXISTS "${KTX_DLL_RELEASE}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${KTX_DLL_RELEASE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying ktx.dll (Release version for all configurations)..."
    )
    message(WARNING "KTX Debug DLL not found at ${KTX_DLL_DEBUG}, using Release version for all configurations")
elseif(EXISTS "${KTX_DLL_DEBUG}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${KTX_DLL_DEBUG}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying ktx.dll (Debug version for all configurations)..."
    )
    message(WARNING "KTX Release DLL not found at ${KTX_DLL_RELEASE}, using Debug version for all configurations")
else()
    message(WARNING "ktx.dll not found in ${KTX_DIR}. Application may fail to run.")
    message(WARNING "  Expected locations:")
    message(WARNING "    - ${KTX_DLL_DEBUG}")
    message(WARNING "    - ${KTX_DLL_RELEASE}")
endif()

# Copy Vulkan SDK DLL (Debug/Release, fallback if missing)
set(SHADERC_DLL_DEBUG "${VULKAN_SDK}/Bin/shaderc_sharedd.dll")
set(SHADERC_DLL_RELEASE "${VULKAN_SDK}/Bin/shaderc_shared.dll")

if(EXISTS "${SHADERC_DLL_DEBUG}" AND EXISTS "${SHADERC_DLL_RELEASE}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${SHADERC_DLL_DEBUG},${SHADERC_DLL_RELEASE}>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying shaderc_shared DLL ($<CONFIG>)..."
    )
elseif(EXISTS "${SHADERC_DLL_RELEASE}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADERC_DLL_RELEASE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying shaderc_shared.dll (Release version for all configurations)..."
    )
    message(WARNING "shaderc_sharedd.dll not found, using Release version for all configurations")
elseif(EXISTS "${SHADERC_DLL_DEBUG}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADERC_DLL_DEBUG}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying shaderc_sharedd.dll (Debug version for all configurations)..."
    )
else()
    message(WARNING "shaderc_shared DLL not found in ${VULKAN_SDK}/Bin")
endif()

# Copy spirv-cross DLL (if exists)
set(SPIRV_CROSS_DLL "${VULKAN_SDK}/Bin/spirv-cross-c-shared.dll")
if(EXISTS "${SPIRV_CROSS_DLL}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SPIRV_CROSS_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"

        COMMENT "Copying spirv-cross-c-shared.dll..."
    )
endif()

# Visual Studio filter settings (source grouping)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CORE_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SAMPLE_SOURCES})
source_group("ThirdParties\\imgui" FILES ${IMGUI_SOURCES})
source_group("" FILES ${ROOT_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Assets PREFIX "Assets" FILES ${SHADER_FILES})

# Visual Studio startup project and debugging settings
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

# Visual Studio debugger settings
if(MSVC)
    # Convert paths to Windows style
    file(TO_NATIVE_PATH "${VULKAN_SDK}/Bin" VK_LAYER_PATH_NATIVE)
    file(TO_NATIVE_PATH "${VULKAN_SDK}" VULKAN_SDK_NATIVE)
    
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        VS_DEBUGGER_ENVIRONMENT "VK_LAYER_PATH=${VK_LAYER_PATH_NATIVE}\nVULKAN_SDK=${VULKAN_SDK_NATIVE}"
    )
    
    message(STATUS "==================== Debugger Configuration ====================")
    message(STATUS "Working Directory: $<TARGET_FILE_DIR:${PROJECT_NAME}>")
    message(STATUS "VK_LAYER_PATH: ${VK_LAYER_PATH_NATIVE}")
    message(STATUS "VULKAN_SDK: ${VULKAN_SDK_NATIVE}")
    message(STATUS "================================================================")
endif()

message(STATUS "Vulkan SDK: ${VULKAN_SDK}")
message(STATUS "Vulkan Include: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "GLFW Library: ${GLFW_LIBRARY}")
message(STATUS "KTX Library (Debug): ${KTX_LIBRARY_DEBUG}")
message(STATUS "KTX Library (Release): ${KTX_LIBRARY_RELEASE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")