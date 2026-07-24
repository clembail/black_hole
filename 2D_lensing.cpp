#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

#include <CoreGraphics/CoreGraphics.h>
#include <Carbon/Carbon.h>

#include <vector>
#include <iostream>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include "MathHelpers.h"

using namespace std;

double c = 299792458.0;
double G = 6.67430e-11;

struct Ray;
void rk4Step(Ray& ray, double dλ, double rs);

// --- Camera ---
struct Camera2D {
    float width = 100000000000.0f;
    float height = 75000000000.0f;
    float offsetX = 0.0f, offsetY = 0.0f;
    float zoom = 1.0f;
    bool middleMousePressed = false;
    double lastMouseX = 0.0, lastMouseY = 0.0;
    
    void processInput(bool leftClick, double mouseX, double mouseY) {
        if (leftClick) {
            if (!middleMousePressed) {
                middleMousePressed = true;
                lastMouseX = mouseX;
                lastMouseY = mouseY;
            } else {
                float dx = float(mouseX - lastMouseX);
                float dy = float(mouseY - lastMouseY);
                offsetX -= dx * (width / 400.0f);
                offsetY += dy * (height / 300.0f);
                lastMouseX = mouseX;
                lastMouseY = mouseY;
            }
        } else {
            middleMousePressed = false;
        }
    }
    
    void processScroll(float dy) {
        float zoomFactor = 1.0f + dy * 0.05f;
        width *= zoomFactor;
        height *= zoomFactor;
    }
};
Camera2D camera2d;

// --- Physics ---
struct BlackHole {
    simd::float3 position;
    double mass;
    double radius;
    double r_s;

    BlackHole(simd::float3 pos, float m) : position(pos), mass(m) {r_s = 2.0 * G * mass / (c*c);}
    
    void getVertices(vector<float>& outVertices) {
        outVertices.push_back(0.0f); outVertices.push_back(0.0f);
        outVertices.push_back(1.0f); outVertices.push_back(0.0f); outVertices.push_back(0.0f); outVertices.push_back(1.0f);
        for(int i = 0; i <= 100; i++) {
            float angle = 2.0f * M_PI * i / 100;
            float x = r_s * cos(angle);
            float y = r_s * sin(angle);
            outVertices.push_back(x); outVertices.push_back(y);
            outVertices.push_back(1.0f); outVertices.push_back(0.0f); outVertices.push_back(0.0f); outVertices.push_back(1.0f);
        }
    }
};
BlackHole SagA(simd_make_float3(0.0f, 0.0f, 0.0f), 8.54e36);

struct Ray{
    double x;   double y;
    double r;   double phi;
    double dr;  double dphi;
    vector<simd::float2> trail;
    double E, L;

    Ray(simd::float2 pos, simd::float2 dir) : x(pos.x), y(pos.y), r(sqrt(pos.x * pos.x + pos.y * pos.y)), phi(atan2(pos.y, pos.x)), dr(dir.x), dphi(dir.y) {
        this->r = sqrt(x*x + y*y);
        this->phi = atan2(y, x);
        dr = dir.x * cos(phi) + dir.y * sin(phi);
        dphi  = ( -dir.x * sin(phi) + dir.y * cos(phi) ) / r;
        L = r*r * dphi;
        double f = 1.0 - SagA.r_s/r;  
        double dt_dλ = sqrt( (dr*dr)/(f*f) + (r*r*dphi*dphi)/f );
        E = f * dt_dλ;
        trail.push_back(simd_make_float2(x, y));
    }
    void step(double dλ, double rs) {
        if(r <= rs) return;
        rk4Step(*this, dλ, rs);
        x = r * cos(phi);
        y = r * sin(phi);
        trail.push_back(simd_make_float2(float(x), float(y)));
    }
};
vector<Ray> rays;

void geodesicRHS(const Ray& ray, double rhs[4], double rs) {
    double r    = ray.r;
    double dr   = ray.dr;
    double dphi = ray.dphi;
    double E    = ray.E;

    double f = 1.0 - rs/r;
    rhs[0] = dr;
    rhs[1] = dphi;
    double dt_dλ = E / f;
    rhs[2] = 
        - (rs/(2*r*r)) * f * (dt_dλ*dt_dλ)
        + (rs/(2*r*r*f)) * (dr*dr)
        + (r - rs) * (dphi*dphi);
    rhs[3] = -2.0 * dr * dphi / r;
}
void addState(const double a[4], const double b[4], double factor, double out[4]) {
    for (int i = 0; i < 4; i++)
        out[i] = a[i] + b[i] * factor;
}
void rk4Step(Ray& ray, double dλ, double rs) {
    double y0[4] = { ray.r, ray.phi, ray.dr, ray.dphi };
    double k1[4], k2[4], k3[4], k4[4], temp[4];

    geodesicRHS(ray, k1, rs);
    addState(y0, k1, dλ/2.0, temp);
    Ray r2 = ray; r2.r=temp[0]; r2.phi=temp[1]; r2.dr=temp[2]; r2.dphi=temp[3];
    geodesicRHS(r2, k2, rs);

    addState(y0, k2, dλ/2.0, temp);
    Ray r3 = ray; r3.r=temp[0]; r3.phi=temp[1]; r3.dr=temp[2]; r3.dphi=temp[3];
    geodesicRHS(r3, k3, rs);

    addState(y0, k3, dλ, temp);
    Ray r4 = ray; r4.r=temp[0]; r4.phi=temp[1]; r4.dr=temp[2]; r4.dphi=temp[3];
    geodesicRHS(r4, k4, rs);

    ray.r    += (dλ/6.0)*(k1[0] + 2*k2[0] + 2*k3[0] + k4[0]);
    ray.phi  += (dλ/6.0)*(k1[1] + 2*k2[1] + 2*k3[1] + k4[1]);
    ray.dr   += (dλ/6.0)*(k1[2] + 2*k2[2] + 2*k3[2] + k4[2]);
    ray.dphi += (dλ/6.0)*(k1[3] + 2*k2[3] + 2*k3[3] + k4[3]);
}

// --- Renderer ---
class Renderer : public MTK::ViewDelegate {
public:
    MTL::Device* device;
    MTL::CommandQueue* commandQueue;
    MTL::RenderPipelineState* renderPipelineState;
    MTL::Buffer* uniformsBuffer;
    MTL::Buffer* dynamicVBO;
    int maxDynamicFloats = 1000000;
    
    int WIDTH = 800;
    int HEIGHT = 600;
    
    Renderer(MTL::Device* pDevice) : device(pDevice->retain()) {
        commandQueue = device->newCommandQueue();
        
        NS::Error* error = nullptr;
        
        // Try Xcode's default library first (when building via Xcode)
        MTL::Library* library = device->newDefaultLibrary();
        
        // Fallback for CMake manual compilation
        if (!library) {
            NS::String* path = NS::String::string("lensing2d.metallib", NS::UTF8StringEncoding);
            library = device->newLibrary(path, &error);
        }
        
        if (!library) {
            std::cerr << "Failed to load lensing2d.metallib\n";
            exit(1);
        }
        
        MTL::Function* vertFunc = library->newFunction(NS::String::string("color_vertex", NS::UTF8StringEncoding));
        MTL::Function* fragFunc = library->newFunction(NS::String::string("color_fragment", NS::UTF8StringEncoding));
        
        MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
        desc->setVertexFunction(vertFunc);
        desc->setFragmentFunction(fragFunc);
        desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
        desc->colorAttachments()->object(0)->setBlendingEnabled(true);
        desc->colorAttachments()->object(0)->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        desc->colorAttachments()->object(0)->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        desc->colorAttachments()->object(0)->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
        desc->colorAttachments()->object(0)->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        
        MTL::VertexDescriptor* vd = MTL::VertexDescriptor::alloc()->init();
        vd->attributes()->object(0)->setFormat(MTL::VertexFormatFloat2);
        vd->attributes()->object(0)->setOffset(0);
        vd->attributes()->object(0)->setBufferIndex(0);
        vd->attributes()->object(1)->setFormat(MTL::VertexFormatFloat4);
        vd->attributes()->object(1)->setOffset(2*sizeof(float));
        vd->attributes()->object(1)->setBufferIndex(0);
        vd->layouts()->object(0)->setStride(6*sizeof(float));
        desc->setVertexDescriptor(vd);
        
        renderPipelineState = device->newRenderPipelineState(desc, &error);
        vertFunc->release(); fragFunc->release(); desc->release(); vd->release(); library->release();
        
        uniformsBuffer = device->newBuffer(sizeof(simd::float4x4), MTL::ResourceStorageModeShared);
        dynamicVBO = device->newBuffer(maxDynamicFloats * sizeof(float), MTL::ResourceStorageModeShared);
    }
    
    ~Renderer() {
        dynamicVBO->release();
        uniformsBuffer->release();
        renderPipelineState->release();
        commandQueue->release();
        device->release();
    }
    
    virtual void drawableSizeWillChange(MTK::View* pView, CGSize size) override {
        WIDTH = size.width;
        HEIGHT = size.height;
    }
    
    virtual void drawInMTKView(MTK::View* pView) override {
        // --- Input Handling ---
        bool isActive = ((bool(*)(id, SEL))objc_msgSend)((id)NS::Application::sharedApplication(), sel_registerName("isActive"));
        if (isActive) {
            CGEventRef event = CGEventCreate(NULL);
            CGPoint mousePos = CGEventGetLocation(event);
            CFRelease(event);
            
            bool leftClick = CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonLeft);
            camera2d.processInput(leftClick, mousePos.x, mousePos.y);
        }
        
        // --- Physics ---
        for (auto& ray : rays) {
            ray.step(1.0f, SagA.r_s);
        }
        
        // --- Rendering ---
        CA::MetalDrawable* drawable = pView->currentDrawable();
        if (!drawable) return;
        
        MTL::CommandBuffer* cmdBuf = commandQueue->commandBuffer();
        
        MTL::RenderPassDescriptor* passDesc = pView->currentRenderPassDescriptor();
        if (passDesc != nullptr) {
            MTL::RenderCommandEncoder* ren = cmdBuf->renderCommandEncoder(passDesc);
            ren->setRenderPipelineState(renderPipelineState);
            
            double left = -camera2d.width + camera2d.offsetX;
            double right = camera2d.width + camera2d.offsetX;
            double bottom = -camera2d.height + camera2d.offsetY;
            double top = camera2d.height + camera2d.offsetY;
            simd::float4x4 proj = math::ortho((float)left, (float)right, (float)bottom, (float)top, -1.0f, 1.0f);
            
            memcpy(uniformsBuffer->contents(), &proj, sizeof(simd::float4x4));
            ren->setVertexBuffer(uniformsBuffer, 0, 1);
            
            // Draw black hole
            vector<float> bhVerts;
            SagA.getVertices(bhVerts);
            MTL::Buffer* bhBuf = device->newBuffer(bhVerts.data(), bhVerts.size()*sizeof(float), MTL::ResourceStorageModeShared);
            ren->setVertexBuffer(bhBuf, 0, 0);
            ren->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(102));
            bhBuf->release();
            
            // Draw rays
            float* vboData = (float*)dynamicVBO->contents();
            int currentFloatOffset = 0;

            for (const auto& ray : rays) {
                size_t N = ray.trail.size();
                if (N < 2) continue;
                
                int requiredFloats = (N * 6) + 6; // 6 floats per trail vertex, 6 for the head point
                if (currentFloatOffset + requiredFloats > maxDynamicFloats) break; // prevent overflow

                int trailOffset = currentFloatOffset;
                for (size_t i = 0; i < N; ++i) {
                    float alpha = float(i) / float(N - 1);
                    vboData[currentFloatOffset++] = (float)ray.trail[i].x;
                    vboData[currentFloatOffset++] = (float)ray.trail[i].y;
                    vboData[currentFloatOffset++] = 1.0f;
                    vboData[currentFloatOffset++] = 1.0f;
                    vboData[currentFloatOffset++] = 1.0f;
                    vboData[currentFloatOffset++] = std::max(alpha, 0.05f);
                }
                
                ren->setVertexBuffer(dynamicVBO, trailOffset * sizeof(float), 0);
                ren->drawPrimitives(MTL::PrimitiveTypeLineStrip, NS::UInteger(0), NS::UInteger(N));
                
                // head point
                int headOffset = currentFloatOffset;
                vboData[currentFloatOffset++] = (float)ray.x;
                vboData[currentFloatOffset++] = (float)ray.y;
                vboData[currentFloatOffset++] = 1.0f;
                vboData[currentFloatOffset++] = 0.0f;
                vboData[currentFloatOffset++] = 0.0f;
                vboData[currentFloatOffset++] = 1.0f;
                
                ren->setVertexBuffer(dynamicVBO, headOffset * sizeof(float), 0);
                ren->drawPrimitives(MTL::PrimitiveTypePoint, NS::UInteger(0), NS::UInteger(1));
            }
            ren->endEncoding();
        }
        cmdBuf->presentDrawable(drawable);
        cmdBuf->commit();
    }
};

void scrollWheelImpl(id self, SEL cmd, id event) {
    double dy = ((double(*)(id, SEL))objc_msgSend)(event, sel_registerName("scrollingDeltaY"));
    camera2d.processScroll(static_cast<float>(dy));
}

// --- App Delegate ---
class AppDelegate : public NS::ApplicationDelegate {
public:
    NS::Window* _window;
    MTK::View* _mtkView;
    MTL::Device* _device;
    Renderer* _renderer;

    ~AppDelegate() {
        _mtkView->release();
        _window->release();
        _device->release();
        delete _renderer;
    }
    
    virtual void applicationWillFinishLaunching(NS::Notification* notification) override {
        NS::Menu* menu = NS::Menu::alloc()->init();
        NS::MenuItem* appMenuItem = NS::MenuItem::alloc()->init();
        menu->addItem(appMenuItem);
        NS::Application::sharedApplication()->setMainMenu(menu);
        
        NS::Menu* appMenu = NS::Menu::alloc()->init(NS::String::string("App", NS::UTF8StringEncoding));
        NS::String* quitTitle = NS::String::string("Quit", NS::UTF8StringEncoding);
        NS::MenuItem* quitItem = appMenu->addItem(quitTitle, sel_registerName("terminate:"), NS::String::string("q", NS::UTF8StringEncoding));
        appMenuItem->setSubmenu(appMenu);
    }
    
    virtual void applicationDidFinishLaunching(NS::Notification* notification) override {
        CGRect frame = (CGRect){ {100, 100}, {800, 600} };
        _window = NS::Window::alloc()->init(frame, NS::WindowStyleMaskClosable|NS::WindowStyleMaskTitled|NS::WindowStyleMaskResizable, NS::BackingStoreBuffered, false);
        _window->setTitle(NS::String::string("Black Hole 2D (100% Metal)", NS::UTF8StringEncoding));
        
        _device = MTL::CreateSystemDefaultDevice();
        
        static Class customViewClass = nullptr;
        if (!customViewClass) {
            Class mtkViewClass = objc_getClass("MTKView");
            customViewClass = objc_allocateClassPair(mtkViewClass, "CustomMTKView2D", 0);
            class_addMethod(customViewClass, sel_registerName("scrollWheel:"), (IMP)scrollWheelImpl, "v@:@");
            objc_registerClassPair(customViewClass);
        }
        
        id viewAlloc = ((id(*)(Class, SEL))objc_msgSend)(customViewClass, sel_registerName("alloc"));
        _mtkView = ((MTK::View*)viewAlloc)->init(frame, _device);
        
        _mtkView->setColorPixelFormat(MTL::PixelFormatBGRA8Unorm);
        _mtkView->setClearColor(MTL::ClearColor::Make(0,0,0,1));
        
        _renderer = new Renderer(_device);
        _mtkView->setDelegate(_renderer);
        
        _window->setContentView(_mtkView);
        _window->makeKeyAndOrderFront(nullptr);
    }
    
    virtual bool applicationShouldTerminateAfterLastWindowClosed(NS::Application* sender) override {
        return true;
    }
};

int main() {
    for (int i = 0; i < 20; ++i) {
        float y = 1.0e10f + i * 2.5e9f;
        rays.push_back(Ray(simd_make_float2(-1e11f, y), simd_make_float2(c, 0.0f)));
    }

    NS::Application* app = NS::Application::sharedApplication();
    app->setActivationPolicy(NS::ActivationPolicyRegular);
    app->activateIgnoringOtherApps(true);
    AppDelegate delegate;
    app->setDelegate(&delegate);
    app->run();
    return 0;
}
