#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

class Renderer : public MTK::ViewDelegate {
public:
    Renderer() {}
    virtual void drawInMTKView(MTK::View* pView) override {}
    virtual void drawableSizeWillChange(MTK::View* pView, CGSize size) override {}
};

int main() {
    Renderer r;
    return 0;
}
