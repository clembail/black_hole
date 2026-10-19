cmake_minimum_required(VERSION 3.21)
project(BlackHoleSim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suppress errors from CoreFoundation headers in newer macOS SDKs / Clang versions
add_compile_options(-Wno-elaborated-enum-base)

# metal-cpp headers. Override with -DMETAL_CPP_ROOT=... if installed elsewhere.
set(METAL_CPP_ROOT "/opt/metal" CACHE PATH "Directory containing metal-cpp and metal-cpp-extensions")
set(METAL_CPP_INCLUDE
    "${METAL_CPP_ROOT}/metal-cpp"
    "${METAL_CPP_ROOT}/metal-cpp-extensions"
)

foreach(header_dir IN LISTS METAL_CPP_INCLUDE)
    if(NOT EXISTS "${header_dir}")
        message(FATAL_ERROR
            "metal-cpp not found at ${header_dir}\n"
            "Install it there, or configure with -DMETAL_CPP_ROOT=/path/to/parent")
    endif()
endforeach()

# Find required Apple frameworks
find_library(METAL_FRAMEWORK Metal)
find_library(FOUNDATION_FRAMEWORK Foundation)
find_library(COCOA_FRAMEWORK Cocoa)
find_library(QUARTZCORE_FRAMEWORK QuartzCore)
find_library(APPKIT_FRAMEWORK AppKit)
find_library(METALKIT_FRAMEWORK MetalKit)

set(METAL_DEPS
    ${METAL_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${COCOA_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
    ${APPKIT_FRAMEWORK}
    ${METALKIT_FRAMEWORK}
)

# Everything the build produces (executables and .metallib) goes here, so the
# source tree stays clean and the two stay next to each other.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(metal_impl STATIC src/metal_impl.cpp)
target_include_directories(metal_impl PUBLIC ${METAL_CPP_INCLUDE})
target_link_libraries(metal_impl PUBLIC ${METAL_DEPS})

# 2D Lensing executable
add_executable(BlackHole2D src/2D_lensing.cpp)
target_link_libraries(BlackHole2D PRIVATE metal_impl)
target_include_directories(BlackHole2D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${METAL_CPP_INCLUDE})

# 3D Black Hole executable
add_executable(BlackHole3D src/black_hole.cpp)
target_link_libraries(BlackHole3D PRIVATE metal_impl)
target_include_directories(BlackHole3D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${METAL_CPP_INCLUDE})

# Shader compilation
#
# Declared as real outputs rather than POST_BUILD commands so that editing a
# .metal file triggers a rebuild. Previously the shaders were only recompiled
# when the C++ target happened to relink, so a shader-only edit was silently
# ignored.
function(add_metal_library target src out_name)
    get_filename_component(src_abs "${src}" ABSOLUTE)
    set(air "${CMAKE_BINARY_DIR}/${target}_${out_name}.air")
    set(lib "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${out_name}")

    add_custom_command(
        OUTPUT "${lib}"
        COMMAND xcrun -sdk macosx metal -c "${src_abs}" -o "${air}"
        COMMAND xcrun -sdk macosx metallib "${air}" -o "${lib}"
        COMMAND ${CMAKE_COMMAND} -E remove "${air}"
        DEPENDS "${src_abs}"
        COMMENT "Compiling ${out_name}"
        VERBATIM
    )

    add_custom_target(${target}_shaders DEPENDS "${lib}")
    add_dependencies(${target} ${target}_shaders)
endfunction()

add_metal_library(BlackHole3D src/shaders.metal shaders.metallib)
add_metal_library(BlackHole2D src/lensing2d.metal lensing2d.metallib)
