# **black**_**hole**

A fully native macOS 3D and 2D Black Hole simulator, rewritten to use Apple's **Metal** graphics API and **AppKit**, delivering real-time relativistic raytracing of spacetime.

## Features

1. **Ray-tracing**: Simulates gravitational lensing using Runge-Kutta 4 integration directly on the GPU Compute Shaders.
2. **Accretion Disk**: Simulates an accretion disk that interacts visually with the black hole's gravity.
3. **Spacetime Curvature**: Demonstrates visually the "trapdoor in spacetime" (Flamm's paraboloid) using a dynamic spacetime grid.
4. **100% Native macOS**: Uses `metal-cpp` and AppKit for native windowing, trackpad scrolling, and GPU acceleration without heavy dependencies like GLFW or OpenGL.

---

## **Building Requirements**

1. A Mac (Apple Silicon or Intel).
2. [CMake](https://cmake.org/) (if building from terminal) or Xcode.
3. Apple's `metal-cpp` and `metal-cpp-extensions`.

### **Installing `metal-cpp`**
Because this project is written in C++ but uses macOS native frameworks, you need Apple's C++ headers for Metal and AppKit.
1. Download `metal-cpp` from Apple's developer portal.
2. Place the `metal-cpp` and `metal-cpp-extensions` folders into `/opt/metal/`. 
   (If you prefer another location, just update the paths in `CMakeLists.txt` or your Xcode project's Header Search Paths).

---

## **Build Instructions (Terminal / CMake)**

1. Clone the repository:
   ```bash
   git clone https://github.com/clembail/black_hole.git
   cd black_hole
   ```

2. Create a build directory and configure:
   ```bash
   cmake -B build
   ```

3. Compile the project:
   ```bash
   make -C build
   ```

4. Run the simulation:
   ```bash
   cd build
   ./BlackHole3D   # For the 3D Spacetime & Raytracing Simulation
   ./BlackHole2D   # For the 2D Gravitational Lensing Simulation
   ```

---

## **Build Instructions (Xcode)**

1. Create a new "Command Line Tool" C++ project in Xcode.
2. Drag and drop `black_hole.cpp`, `metal_impl.cpp`, `MathHelpers.h`, and `shaders.metal` into your Xcode project. (Replace the default `main.cpp`).
3. Under **Build Settings** > **Header Search Paths**, add:
   - `/opt/metal/metal-cpp`
   - `/opt/metal/metal-cpp-extensions`
4. Under **Build Phases** > **Link Binary With Libraries**, add:
   - `Metal.framework`
   - `Foundation.framework`
   - `Cocoa.framework`
   - `QuartzCore.framework`
   - `AppKit.framework`
   - `MetalKit.framework`
5. Change the **C++ Language Dialect** to `C++17` (or newer).
6. Press the Play button! Xcode will automatically compile `shaders.metal` into its default `.metallib` and run the simulation natively.

---

## **Controls**

- **Left Click + Drag**: Rotate the camera around the black hole.
- **Scroll (Mouse Wheel / Two-finger Trackpad)**: Zoom in and out.
- **Key 'G'**: Toggle Gravity on and off for the surrounding celestial objects (they will start orbiting the black hole and bending spacetime dynamically).
