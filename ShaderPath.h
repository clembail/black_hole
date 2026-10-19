#pragma once

#include <mach-o/dyld.h>

#include <climits>
#include <cstdlib>
#include <string>
#include <vector>

namespace shader {

// Directory containing the running executable.
//
// The .metallib is emitted next to the executable by the CMake build, but it
// used to be opened through a bare relative path. That only worked if the
// process happened to be started from the directory holding the library, so
// running "./build/BlackHole3D" from the repo root silently picked up a stale
// shaders.metallib sitting in the current directory instead.
inline std::string executableDir() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size > 0 ? size : 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};

    char resolved[PATH_MAX];
    if (!realpath(buf.data(), resolved)) return {};

    std::string path(resolved);
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return {};
    return path.substr(0, slash);
}

// Absolute path of a shader library shipped alongside the executable.
inline std::string libraryPath(const char* name) {
    const std::string dir = executableDir();
    if (dir.empty()) return std::string(name);
    return dir + "/" + name;
}

} // namespace shader
