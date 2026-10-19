# legacy/

The original OpenGL/C++ version of the project, kept for reference.

These files are **not part of the build**. They require OpenGL, GLFW, GLEW and
GLM, and predate the Metal port.

| File | What it was |
| --- | --- |
| `ray_tracing.cpp` | The original OpenGL raytracer (spheres only, no geodesics) |
| `CPU-geodesic.cpp` | A CPU implementation of the geodesic integrator, used to validate the GPU version |
| `geodesic.comp` | The GLSL compute shader that the Metal kernel in `src/shaders.metal` derives from |
| `grid.vert`, `grid.frag` | GLSL shaders for the spacetime grid, now `grid_vertex`/`grid_fragment` in `src/shaders.metal` |
| `Gravity_Sim.zip` | A snapshot of an earlier, separate gravity simulation project |

The GLSL compute shader is the most useful reference here: `src/shaders.metal`
started as a direct port of it before being reworked to integrate in the
orbital plane.
