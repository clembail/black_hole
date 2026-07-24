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
set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders.metal")
set(SHADER_LIB "$<TARGET_FILE_DIR:BlackHole3D>/shaders.metallib")

add_custom_command(TARGET BlackHole3D POST_BUILD
    COMMAND xcrun -sdk macosx metal -c ${SHADER_SRC} -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders.air
    COMMAND xcrun -sdk macosx metallib ${CMAKE_CURRENT_SOURCE_DIR}/shaders.air -o ${SHADER_LIB}
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_SOURCE_DIR}/shaders.air
)
add_custom_command(TARGET BlackHole2D POST_BUILD
    COMMAND xcrun -sdk macosx metal -c ${CMAKE_CURRENT_SOURCE_DIR}/lensing2d.metal -o ${CMAKE_CURRENT_SOURCE_DIR}/lensing2d.air
    COMMAND xcrun -sdk macosx metallib ${CMAKE_CURRENT_SOURCE_DIR}/lensing2d.air -o $<TARGET_FILE_DIR:BlackHole2D>/lensing2d.metallib
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_SOURCE_DIR}/lensing2d.air
)
