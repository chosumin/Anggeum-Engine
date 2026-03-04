# CopyShaderSpv.cmake
# Usage: cmake -DSRC_DIR=<source> -DDST_DIR=<destination> -P CopyShaderSpv.cmake
#
# Copies only .spv files from SRC_DIR to DST_DIR (preserving directory structure).
# Removes .spv files in DST_DIR that no longer exist in SRC_DIR.
# Removes shader source files (.vert/.frag/.comp/.geom/.tesc/.tese/.glsl/.h) from DST_DIR.

if(NOT SRC_DIR OR NOT DST_DIR)
    message(FATAL_ERROR "SRC_DIR and DST_DIR must be defined")
endif()

# Step 1: Copy new/changed .spv files
file(GLOB_RECURSE SPV_FILES RELATIVE "${SRC_DIR}" "${SRC_DIR}/*.spv")
foreach(FILE ${SPV_FILES})
    set(SRC_FILE "${SRC_DIR}/${FILE}")
    set(DST_FILE "${DST_DIR}/${FILE}")

    # Skip if destination is up-to-date
    if(EXISTS "${DST_FILE}")
        file(TIMESTAMP "${SRC_FILE}" SRC_TIME "%Y%m%d%H%M%S" UTC)
        file(TIMESTAMP "${DST_FILE}" DST_TIME "%Y%m%d%H%M%S" UTC)
        if(SRC_TIME STREQUAL DST_TIME)
            continue()
        endif()
    endif()

    get_filename_component(DST_PARENT "${DST_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${DST_PARENT}")
    file(COPY_FILE "${SRC_FILE}" "${DST_FILE}")
    message(STATUS "Copied: ${FILE}")
endforeach()

# Step 2: Remove .spv files in destination that no longer exist in source
file(GLOB_RECURSE DST_SPV_FILES RELATIVE "${DST_DIR}" "${DST_DIR}/*.spv")
foreach(FILE ${DST_SPV_FILES})
    if(NOT EXISTS "${SRC_DIR}/${FILE}")
        file(REMOVE "${DST_DIR}/${FILE}")
        message(STATUS "Removed stale: ${FILE}")
    endif()
endforeach()

# Step 3: Remove shader source files from destination (not needed at runtime)
set(SHADER_SOURCE_EXTENSIONS "*.vert" "*.frag" "*.comp" "*.geom" "*.tesc" "*.tese" "*.glsl" "*.h")
foreach(EXT ${SHADER_SOURCE_EXTENSIONS})
    file(GLOB_RECURSE UNWANTED_FILES "${DST_DIR}/${EXT}")
    foreach(FILE ${UNWANTED_FILES})
        file(REMOVE "${FILE}")
        message(STATUS "Removed shader source: ${FILE}")
    endforeach()
endforeach()