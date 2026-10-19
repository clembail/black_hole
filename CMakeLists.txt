cmake_minimum_required(VERSION 3.21)
project(BlackHoleSim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suppress errors from CoreFoundation headers in newer macOS SDKs / Clang versions
add_compile_options(-Wno-elaborated-enum-base)

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

add_library(metal_impl STATIC metal_impl.cpp)
target_include_directories(metal_impl PUBLIC 
    /opt/metal/metal-cpp 
    /opt/metal/metal-cpp-extensions
)
target_link_libraries(metal_impl PUBLIC ${METAL_DEPS})

# 2D Lensing executable
add_executable(BlackHole2D 2D_lensing.cpp)
target_link_libraries(BlackHole2D PRIVATE metal_impl)
target_include_directories(BlackHole2D PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    /opt/metal/metal-cpp 
    /opt/metal/metal-cpp-extensions
)

# 3D Black Hole executable
add_executable(BlackHole3D black_hole.cpp)
target_link_libraries(BlackHole3D PRIVATE metal_impl)
target_include_directories(BlackHole3D PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    /opt/metal/metal-cpp
    /opt/metal/metal-cpp-extensions
)

# Shader compilation
#
# Declared as real outputs rather than POST_BUILD commands so that editing a
# .metal file triggers a rebuild. Previously the shaders were only recompiled
# when the C++ target happened to relink, so a shader-only edit was silently
# ignored.
#
# The .metallib must sit next to the executable, so pin both to the same
# directory (generator expressions are not usable in OUTPUT).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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

add_metal_library(BlackHole3D shaders.metal shaders.metallib)
add_metal_library(BlackHole2D lensing2d.metal lensing2d.metallib)
