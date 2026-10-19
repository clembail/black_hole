# scratch/

Small standalone experiments used while building the Metal port. They are not
part of the build and are not referenced by CMake.

| File | Purpose |
| --- | --- |
| `test_appkit.cpp` | Minimal AppKit window bootstrap |
| `test_mtk.cpp` | Minimal `MTKView` setup |
| `test_bundle.cpp` | Checking Metal library/bundle loading behaviour |
| `test_blocks.mm` | Objective-C block syntax for a scroll-wheel monitor |
| `test_objc.cpp` | Runtime `objc_msgSend` calls to AppKit selectors |

These are kept because they document how the app's AppKit/MetalKit plumbing was
figured out. They can be deleted without affecting the project.
