cmake_minimum_required(VERSION 3.20)
project(AnggeumEngine VERSION 1.0.0 LANGUAGES CXX)

# C++17 표준 설정
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 출력 디렉토리 설정
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Vulkan SDK 경로 (환경 변수에서 가져오거나 직접 설정)
if(DEFINED ENV{VULKAN_SDK})
    set(VULKAN_SDK $ENV{VULKAN_SDK})
else()
    # ★ 여기를 올바른 경로로 수정! ★
    set(VULKAN_SDK "C:/VulkanSDK/1.3.275.0")
endif()

# Vulkan SDK 존재 확인
if(NOT EXISTS "${VULKAN_SDK}")
    message(FATAL_ERROR "Vulkan SDK not found at ${VULKAN_SDK}. Please check the path or set VULKAN_SDK environment variable.")
endif()

message(STATUS "Using Vulkan SDK: ${VULKAN_SDK}")

# Vulkan 패키지 찾기
find_package(Vulkan REQUIRED)

# GLFW 설정 (바이너리 버전 사용)
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/glfw-3.3.8.bin.WIN64")
set(GLFW_INCLUDE_DIR "${GLFW_DIR}/include")

# 64비트 라이브러리 경로 설정
if(MSVC)
    # Visual Studio 버전에 맞는 라이브러리 선택
    set(GLFW_LIBRARY "${GLFW_DIR}/lib-vc2022/glfw3.lib")
else()
    set(GLFW_LIBRARY "${GLFW_DIR}/lib-mingw-w64/libglfw3.a")
endif()

# KTX 라이브러리 설정
set(KTX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/ktx")
set(KTX_INCLUDE_DIR "${KTX_DIR}/include")

# Debug/Release에 따라 다른 KTX 라이브러리 사용
if(EXISTS "${KTX_DIR}/Debug/ktx.lib")
    set(KTX_LIBRARY_DEBUG "${KTX_DIR}/Debug/ktx.lib")
else()
    set(KTX_LIBRARY_DEBUG "${KTX_DIR}/Release/ktx.lib")
    message(WARNING "KTX Debug library not found, using Release version")
endif()
set(KTX_LIBRARY_RELEASE "${KTX_DIR}/Release/ktx.lib")

# Vulkan SDK 라이브러리 경로 설정 (Debug/Release 구분)
set(VULKAN_LIB_DIR "${VULKAN_SDK}/Lib")

# 소스 파일 수집
file(GLOB_RECURSE CORE_SOURCES
    "Core/*.cpp"
    "Core/*.h"
)

file(GLOB_RECURSE SAMPLE_SOURCES
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
    "Application.cpp"
    "Application.h"
    "stdafx.h"
    "stdafx.cpp"
)

# 실행 파일 생성
add_executable(${PROJECT_NAME}
    ${ROOT_SOURCES}
    ${CORE_SOURCES}
    ${SAMPLE_SOURCES}
    ${IMGUI_SOURCES}
)

# Precompiled Header 설정 (CMake 3.16+)
target_precompile_headers(${PROJECT_NAME} PRIVATE "stdafx.h")

# Include 디렉토리 설정
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
)

# 라이브러리 링크 (Debug/Release 구분)
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
)

# 컴파일러별 설정
if(MSVC)
    # MSVC 컴파일러 옵션
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3
        /MP         # 멀티프로세서 컴파일
        /permissive-
    )
    
    # Vulkan 관련 매크로 정의
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
    
    # 런타임 라이브러리 설정
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    
    # Debug 모드 설정
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/RTC1>    # 런타임 체크
        $<$<CONFIG:Debug>:/Od>      # 최적화 비활성화
        $<$<CONFIG:Debug>:/Zi>      # 디버그 정보
    )
    
    # Release 모드 설정
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2>    # 최적화
        $<$<CONFIG:Release>:/Zi>    # 디버그 정보 (선택사항)
        $<$<CONFIG:Release>:/sdl>   # 보안 검사 활성화
    )
    
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
    
else()
    # GCC/Clang 옵션
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        # GCC/Clang에서 자동 초기화
        $<$<CONFIG:Release>:-ftrivial-auto-var-init=zero>
    )
endif()

# 셰이더 파일 복사 (빌드 후)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
    COMMENT "Copying Assets folder..."
)

# Assets 폴더 복사 (빌드 후)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Copying Assets from ${CMAKE_SOURCE_DIR}/Assets to $<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
    COMMENT "Copying Assets folder..."
)

# KTX DLL 복사 (Debug/Release 구분, 없으면 대체)
set(KTX_DLL_DEBUG "${KTX_DIR}/Debug/ktx.dll")
set(KTX_DLL_RELEASE "${KTX_DIR}/Release/ktx.dll")

# Debug DLL이 있으면 구성별로, 없으면 Release 버전 사용
if(EXISTS "${KTX_DLL_DEBUG}" AND EXISTS "${KTX_DLL_RELEASE}")
    # 둘 다 있으면 구성에 맞게 복사
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${KTX_DLL_DEBUG},${KTX_DLL_RELEASE}>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying ktx.dll ($<CONFIG>)..."
    )
elseif(EXISTS "${KTX_DLL_RELEASE}")
    # Release만 있으면 모든 구성에서 Release 사용
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${KTX_DLL_RELEASE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying ktx.dll (Release version for all configurations)..."
    )
    message(WARNING "KTX Debug DLL not found at ${KTX_DLL_DEBUG}, using Release version for all configurations")
elseif(EXISTS "${KTX_DLL_DEBUG}")
    # Debug만 있으면 모든 구성에서 Debug 사용
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

# Vulkan SDK DLL 복사 (Debug/Release 구분, 없으면 대체)
set(SHADERC_DLL_DEBUG "${VULKAN_SDK}/Bin/shaderc_sharedd.dll")
set(SHADERC_DLL_RELEASE "${VULKAN_SDK}/Bin/shaderc_shared.dll")

if(EXISTS "${SHADERC_DLL_DEBUG}" AND EXISTS "${SHADERC_DLL_RELEASE}")
    # 둘 다 있으면 구성에 맞게 복사
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${SHADERC_DLL_DEBUG},${SHADERC_DLL_RELEASE}>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying shaderc_shared DLL ($<CONFIG>)..."
    )
elseif(EXISTS "${SHADERC_DLL_RELEASE}")
    # Release만 있으면 모든 구성에서 Release 사용
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADERC_DLL_RELEASE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying shaderc_shared.dll (Release version for all configurations)..."
    )
    message(WARNING "shaderc_sharedd.dll not found, using Release version for all configurations")
elseif(EXISTS "${SHADERC_DLL_DEBUG}")
    # Debug만 있으면 모든 구성에서 Debug 사용
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADERC_DLL_DEBUG}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying shaderc_sharedd.dll (Debug version for all configurations)..."
    )
else()
    message(WARNING "shaderc_shared DLL not found in ${VULKAN_SDK}/Bin")
endif()

# spirv-cross DLL 복사 (존재하는 경우만)
set(SPIRV_CROSS_DLL "${VULKAN_SDK}/Bin/spirv-cross-c-shared.dll")
if(EXISTS "${SPIRV_CROSS_DLL}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SPIRV_CROSS_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying spirv-cross-c-shared.dll..."
    )
endif()

# Visual Studio 필터 설정 (소스 그룹화)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CORE_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SAMPLE_SOURCES})
source_group("ThirdParties\\imgui" FILES ${IMGUI_SOURCES})
source_group("" FILES ${ROOT_SOURCES})

# Visual Studio 시작 프로젝트 및 디버깅 설정
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

# Visual Studio 디버깅 작업 디렉토리를 실행 파일 위치로 설정
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        # Vulkan Layer 경로를 환경 변수로 설정
        VS_DEBUGGER_ENVIRONMENT "VK_LAYER_PATH=${VULKAN_SDK}/Bin\nVULKAN_SDK=${VULKAN_SDK}\nPATH=${VULKAN_SDK}/Bin;%PATH%"
    )
endif()

# Visual Studio 디버깅 설정
if(MSVC)
    # 경로를 Windows 스타일로 변환
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