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
#include <limits>

#include "MathHelpers.h"
#include "ShaderPath.h"

using namespace std;

// --- Constants ---
double c = 299792458.0;
double G = 6.67430e-11;

// --- Physics State ---
bool Gravity = false;
bool prevKeyG = false;

struct Camera {
    simd::float3 target = {0.0f, 0.0f, 0.0f};
    float radius = 6.34194e10f;
    float minRadius = 1e10f, maxRadius = 1e12f;

    float azimuth = 0.0f;
    float elevation = M_PI / 2.0f - 0.15f;

    float orbitSpeed = 0.01f;
    float panSpeed = 0.01f;
    float zoomSpeed = 25e9f;

    bool dragging = false;
    bool panning = false;
    bool moving = false;
    double lastX = 0.0, lastY = 0.0;

    simd::float3 position() const {
        float clampedElevation = std::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
        return {
            radius * sinf(clampedElevation) * cosf(azimuth),
            radius * cosf(clampedElevation),
            radius * sinf(clampedElevation) * sinf(azimuth)
        };
    }
    void update() {
        target = {0.0f, 0.0f, 0.0f};
        moving = dragging | panning;
    }

    void processInput(bool leftClick, double mouseX, double mouseY) {
        if (leftClick) {
            if (!dragging) {
                dragging = true;
                lastX = mouseX;
                lastY = mouseY;
            } else {
                float dx = float(mouseX - lastX);
                float dy = float(mouseY - lastY);
                azimuth   += dx * orbitSpeed;
                elevation -= dy * orbitSpeed;
                elevation = std::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
                lastX = mouseX;
                lastY = mouseY;
            }
        } else {
            dragging = false;
        }
        update();
    }
    void processScroll(float dy) {
        radius -= dy * zoomSpeed * 0.05f;
        radius = std::clamp(radius, minRadius, maxRadius);
        update();
    }
};
Camera camera;

struct BlackHole {
    simd::float3 position;
    double mass;
    double radius;
    double r_s;

    BlackHole(simd::float3 pos, float m) : position(pos), mass(m) {r_s = 2.0 * G * mass / (c*c);}
    bool Intercept(float px, float py, float pz) const {
        double dx = double(px) - double(position.x);
        double dy = double(py) - double(position.y);
        double dz = double(pz) - double(position.z);
        double dist2 = dx * dx + dy * dy + dz * dz;
        return dist2 < r_s * r_s;
    }
};
BlackHole SagA(simd_make_float3(0.0f, 0.0f, 0.0f), 8.54e36);

struct ObjectData {
    simd::float4 posRadius;
    simd::float4 color;
    float  mass;
    simd::float3 velocity;
};
vector<ObjectData> objects = {
    { simd_make_float4(4e11f, 0.0f, 0.0f, 4e10f)   , simd_make_float4(1,1,0,1), 1.98892e30, simd_make_float3(0,0,0) },
    { simd_make_float4(0.0f, 0.0f, 4e11f, 4e10f)   , simd_make_float4(1,0,0,1), 1.98892e30, simd_make_float3(0,0,0) },
    { simd_make_float4(0.0f, 0.0f, 0.0f, (float)SagA.r_s) , simd_make_float4(0,0,0,1), static_cast<float>(SagA.mass), simd_make_float3(0,0,0) }
};

// --- Renderer ---
class Renderer : public MTK::ViewDelegate {
public:
    MTL::Device* device;
    MTL::CommandQueue* commandQueue;
    
    MTL::ComputePipelineState* computePipelineState;
    MTL::RenderPipelineState* quadPipelineState;
    MTL::RenderPipelineState* gridPipelineState;
    
    MTL::Texture* renderTexture = nullptr;
    
    MTL::Buffer* cameraUBO;
    MTL::Buffer* diskUBO;
    MTL::Buffer* objectsUBO;
    MTL::Buffer* gridUniformsBuffer;
    
    MTL::Buffer* gridVBO = nullptr;
    MTL::Buffer* gridEBO = nullptr;
    int gridIndexCount = 0;
    bool gridInitialized = false;
    
    MTL::Buffer* quadVBO;
    
    int WIDTH = 800;
    int HEIGHT = 600;
    // The raytrace runs at drawable resolution. It used to be fixed at
    // 200x150, which threw away almost all of the display's detail.
    int COMPUTE_WIDTH  = 800;
    int COMPUTE_HEIGHT = 600;

    // While the camera is moving, render into a smaller texture and let the
    // fullscreen quad upscale it. Motion hides the reduced detail and the
    // compute cost drops with the pixel count.
    float renderScale = 1.0f;
    float interactiveScale = 0.5f;

    // Set whenever something that affects the image changed. When it stays
    // clear the expensive compute pass is skipped entirely and the previous
    // result is presented again.
    bool needsRedraw = true;
    simd::float3 lastCamPos = {0.0f, 0.0f, 0.0f};
    
    Renderer(MTL::Device* pDevice) : device(pDevice->retain()) {
        commandQueue = device->newCommandQueue();
        
        NS::Error* error = nullptr;
        
        // Try Xcode's default library first (when building via Xcode)
        MTL::Library* library = device->newDefaultLibrary();
        
        // Fallback for CMake manual compilation. Resolve the path relative to
        // the executable rather than the working directory, so the binary can
        // be launched from anywhere.
        if (!library) {
            std::string libPath = shader::libraryPath("shaders.metallib");
            NS::String* path = NS::String::string(libPath.c_str(), NS::UTF8StringEncoding);
            library = device->newLibrary(path, &error);
            if (!library) std::cerr << "Looked for shaders.metallib at: " << libPath << "\n";
        }
        
        if (!library) {
            std::cerr << "Failed to load shaders.metallib\n";
            exit(1);
        }
        
        // Compute Pipeline
        MTL::Function* computeFunc = library->newFunction(NS::String::string("geodesic", NS::UTF8StringEncoding));
        computePipelineState = device->newComputePipelineState(computeFunc, &error);
        computeFunc->release();
        
        // Quad Pipeline
        MTL::Function* quadVert = library->newFunction(NS::String::string("quad_vertex", NS::UTF8StringEncoding));
        MTL::Function* quadFrag = library->newFunction(NS::String::string("quad_fragment", NS::UTF8StringEncoding));
        MTL::RenderPipelineDescriptor* quadDesc = MTL::RenderPipelineDescriptor::alloc()->init();
        quadDesc->setVertexFunction(quadVert);
        quadDesc->setFragmentFunction(quadFrag);
        quadDesc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
        
        MTL::VertexDescriptor* quadVD = MTL::VertexDescriptor::alloc()->init();
        quadVD->attributes()->object(0)->setFormat(MTL::VertexFormatFloat2);
        quadVD->attributes()->object(0)->setOffset(0);
        quadVD->attributes()->object(0)->setBufferIndex(0);
        quadVD->attributes()->object(1)->setFormat(MTL::VertexFormatFloat2);
        quadVD->attributes()->object(1)->setOffset(2 * sizeof(float));
        quadVD->attributes()->object(1)->setBufferIndex(0);
        quadVD->layouts()->object(0)->setStride(4 * sizeof(float));
        quadDesc->setVertexDescriptor(quadVD);
        
        quadPipelineState = device->newRenderPipelineState(quadDesc, &error);
        quadVert->release(); quadFrag->release(); quadDesc->release(); quadVD->release();
        
        // Grid Pipeline
        MTL::Function* gridVert = library->newFunction(NS::String::string("grid_vertex", NS::UTF8StringEncoding));
        MTL::Function* gridFrag = library->newFunction(NS::String::string("grid_fragment", NS::UTF8StringEncoding));
        MTL::RenderPipelineDescriptor* gridDesc = MTL::RenderPipelineDescriptor::alloc()->init();
        gridDesc->setVertexFunction(gridVert);
        gridDesc->setFragmentFunction(gridFrag);
        gridDesc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
        gridDesc->colorAttachments()->object(0)->setBlendingEnabled(true);
        gridDesc->colorAttachments()->object(0)->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        gridDesc->colorAttachments()->object(0)->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        gridDesc->colorAttachments()->object(0)->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
        gridDesc->colorAttachments()->object(0)->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        
        MTL::VertexDescriptor* gridVD = MTL::VertexDescriptor::alloc()->init();
        gridVD->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        gridVD->attributes()->object(0)->setOffset(0);
        gridVD->attributes()->object(0)->setBufferIndex(0);
        gridVD->layouts()->object(0)->setStride(sizeof(simd::float3));
        gridDesc->setVertexDescriptor(gridVD);
        
        gridPipelineState = device->newRenderPipelineState(gridDesc, &error);
        gridVert->release(); gridFrag->release(); gridDesc->release(); gridVD->release();
        library->release();
        
        // Buffers
        cameraUBO = device->newBuffer(128, MTL::ResourceStorageModeShared);
        diskUBO = device->newBuffer(sizeof(float)*4, MTL::ResourceStorageModeShared);
        size_t objSize = sizeof(int) + 3*sizeof(float) + 16*sizeof(simd::float4)*2 + 16*sizeof(float) + 4*sizeof(float);
        objectsUBO = device->newBuffer(objSize, MTL::ResourceStorageModeShared);
        gridUniformsBuffer = device->newBuffer(sizeof(simd::float4x4), MTL::ResourceStorageModeShared);
        
        float quadVertices[] = {
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };
        quadVBO = device->newBuffer(quadVertices, sizeof(quadVertices), MTL::ResourceStorageModeShared);
        
        reallocateTexture();
    }
    
    ~Renderer() {
        if (renderTexture) renderTexture->release();
        cameraUBO->release();
        diskUBO->release();
        objectsUBO->release();
        gridUniformsBuffer->release();
        quadVBO->release();
        if (gridVBO) gridVBO->release();
        if (gridEBO) gridEBO->release();
        computePipelineState->release();
        quadPipelineState->release();
        gridPipelineState->release();
        commandQueue->release();
        device->release();
    }
    
    void reallocateTexture() {
        if (renderTexture) renderTexture->release();
        MTL::TextureDescriptor* texDesc = MTL::TextureDescriptor::alloc()->init();
        texDesc->setWidth(COMPUTE_WIDTH);
        texDesc->setHeight(COMPUTE_HEIGHT);
        texDesc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
        texDesc->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead);
        renderTexture = device->newTexture(texDesc);
        texDesc->release();
        needsRedraw = true;
    }

    void generateGrid(const vector<ObjectData>& objects, bool forceUpdate = false) {
        if (gridInitialized && !Gravity && !forceUpdate) return;

        const int gridSize = 100;
        const float spacing = 2.5e9f;
        const int numVertices = (gridSize + 1) * (gridSize + 1);

        if (!gridVBO) {
            gridVBO = device->newBuffer(numVertices * sizeof(simd::float3), MTL::ResourceStorageModeShared);
        }

        simd::float3* vboData = (simd::float3*)gridVBO->contents();
        int vIdx = 0;

        // Precompute per-object Schwarzschild radius once instead of once per
        // vertex (this loop runs over ~10k vertices every frame while gravity
        // is enabled).
        struct GridSource { float x, z, r_s; };
        vector<GridSource> sources;
        sources.reserve(objects.size());
        for (const auto& obj : objects) {
            sources.push_back({ obj.posRadius.x, obj.posRadius.z,
                                float(2.0 * G * obj.mass / (c * c)) });
        }

        for (int z = 0; z <= gridSize; ++z) {
            for (int x = 0; x <= gridSize; ++x) {
                float worldX = (x - gridSize / 2.0f) * spacing;
                float worldZ = (z - gridSize / 2.0f) * spacing;
                float y = 0.0f;

                for (const auto& src : sources) {
                    float dx = worldX - src.x;
                    float dz = worldZ - src.z;
                    float dist = sqrtf(dx * dx + dz * dz);

                    if (dist > src.r_s) {
                        y += 2.0f * sqrtf(src.r_s * (dist - src.r_s)) - 3e10f;
                    } else {
                        y -= 3e10f;
                    }
                }
                vboData[vIdx++] = simd_make_float3(worldX, y, worldZ);
            }
        }

        if (!gridInitialized) {
            vector<uint32_t> indices;
            for (int z = 0; z <= gridSize; ++z) {
                for (int x = 0; x <= gridSize; ++x) {
                    int i = z * (gridSize + 1) + x;
                    if (x < gridSize) {
                        indices.push_back(i); indices.push_back(i + 1);
                    }
                    if (z < gridSize) {
                        indices.push_back(i); indices.push_back(i + gridSize + 1);
                    }
                }
            }
            gridEBO = device->newBuffer(indices.data(), indices.size() * sizeof(uint32_t), MTL::ResourceStorageModeShared);
            gridIndexCount = indices.size();
            gridInitialized = true;
        }
    }
    
    virtual void drawableSizeWillChange(MTK::View* pView, CGSize size) override {
        WIDTH = size.width;
        HEIGHT = size.height;
        if (WIDTH > 0 && HEIGHT > 0) {
            // Keep the compute texture at a sensible size: full drawable
            // resolution is wasteful on a Retina panel for a fullscreen effect.
            const float scale = renderScale;
            COMPUTE_WIDTH  = std::max(1, int(WIDTH  * scale));
            COMPUTE_HEIGHT = std::max(1, int(HEIGHT * scale));
            reallocateTexture();
        }
    }
    
    virtual void drawInMTKView(MTK::View* pView) override {
        // --- Input Handling ---
        bool isActive = ((bool(*)(id, SEL))objc_msgSend)((id)NS::Application::sharedApplication(), sel_registerName("isActive"));
        if (isActive) {
            CGEventRef event = CGEventCreate(NULL);
            CGPoint mousePos = CGEventGetLocation(event);
            CFRelease(event);
            
            bool leftClick = CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonLeft);
            camera.processInput(leftClick, mousePos.x, mousePos.y);
            
            bool keyG = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kVK_ANSI_G);
            if (keyG && !prevKeyG) {
                Gravity = !Gravity;
                needsRedraw = true;
                std::cout << "[INFO] Gravity turned " << (Gravity ? "ON" : "OFF") << std::endl;
            }
            prevKeyG = keyG;

            // Any camera change invalidates the cached image.
            simd::float3 camPos = camera.position();
            if (camPos.x != lastCamPos.x || camPos.y != lastCamPos.y || camPos.z != lastCamPos.z) {
                lastCamPos = camPos;
                needsRedraw = true;
            }
        }
        
        // --- Physics ---
        // Symplectic leapfrog: compute every acceleration from the positions
        // at the start of the step, then advance. The previous version updated
        // each object's position inside the force loop, so the result depended
        // on iteration order and effectively integrated with a half-step error.
        if (Gravity) {
            const size_t n = objects.size();
            vector<simd::float3> accel(n, simd_make_float3(0.0f, 0.0f, 0.0f));
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = i + 1; j < n; ++j) {
                    float dx = objects[j].posRadius.x - objects[i].posRadius.x;
                    float dy = objects[j].posRadius.y - objects[i].posRadius.y;
                    float dz = objects[j].posRadius.z - objects[i].posRadius.z;
                    float dist2 = dx*dx + dy*dy + dz*dz;
                    if (dist2 <= 0.0f) continue;

                    float dist = sqrt(dist2);
                    // a = G * m_other / r^2, direction along the separation.
                    double aMag = G / (double)dist2;
                    simd::float3 u = simd_make_float3(dx / dist, dy / dist, dz / dist);
                    accel[i] += u * float(aMag * objects[j].mass);
                    accel[j] -= u * float(aMag * objects[i].mass);
                }
            }
            for (size_t i = 0; i < n; ++i) {
                objects[i].velocity += accel[i];
                objects[i].posRadius.x += objects[i].velocity.x;
                objects[i].posRadius.y += objects[i].velocity.y;
                objects[i].posRadius.z += objects[i].velocity.z;
            }
            needsRedraw = true;   // scene is animating
        }
        
        generateGrid(objects);
        
        simd::float4x4 view = math::lookAt(camera.position(), camera.target, {0,1,0});
        simd::float4x4 proj = math::perspective(60.0f * M_PI / 180.0f, float(WIDTH)/float(HEIGHT), 1e9f, 1e14f);
        simd::float4x4 viewProj = simd_mul(proj, view);
        
        // --- Rendering ---
        CA::MetalDrawable* drawable = pView->currentDrawable();
        if (!drawable) return;

        MTL::CommandBuffer* cmdBuf = commandQueue->commandBuffer();
        
        // Compute Pass — skipped when the scene is unchanged, so an idle
        // window costs a blit instead of a full raytrace.
        if (needsRedraw) {
            // Drop resolution while interacting, then restore it once the
            // camera settles. This keeps dragging responsive at full-screen
            // Retina sizes without permanently lowering quality.
            const bool interacting = camera.dragging || camera.panning;
            const float wantedScale = interacting ? interactiveScale : renderScale;
            const int wantedW = std::max(1, int(WIDTH  * wantedScale));
            const int wantedH = std::max(1, int(HEIGHT * wantedScale));
            if (wantedW != COMPUTE_WIDTH || wantedH != COMPUTE_HEIGHT) {
                COMPUTE_WIDTH  = wantedW;
                COMPUTE_HEIGHT = wantedH;
                reallocateTexture();
            }

            MTL::ComputeCommandEncoder* computeEncoder = cmdBuf->computeCommandEncoder();
            computeEncoder->setComputePipelineState(computePipelineState);
            computeEncoder->setTexture(renderTexture, 0);
            
            // Upload Camera
            struct CamData {
                simd::float4 pos;
                simd::float4 right;
                simd::float4 up;
                simd::float4 forward;
                float tanHalfFov, aspect;
                bool moving; int _pad4;
            } camData;
            
            simd::float3 fwd = math::normalize(camera.target - camera.position());
            simd::float3 upVec = {0, 1, 0};
            simd::float3 rightVec = math::normalize(math::cross(fwd, upVec));
            upVec = math::cross(rightVec, fwd);
            
            camData.pos = {camera.position().x, camera.position().y, camera.position().z, 0.0f};
            camData.right = {rightVec.x, rightVec.y, rightVec.z, 0.0f};
            camData.up = {upVec.x, upVec.y, upVec.z, 0.0f};
            camData.forward = {fwd.x, fwd.y, fwd.z, 0.0f};
            camData.tanHalfFov = tanf((60.0f * M_PI / 180.0f) * 0.5f);
            camData.aspect = float(WIDTH) / float(HEIGHT);
            camData.moving = camera.dragging || camera.panning;
            memcpy(cameraUBO->contents(), &camData, sizeof(CamData));
            
            // Upload Disk
            float diskData[4] = { (float)(SagA.r_s * 2.2f), (float)(SagA.r_s * 5.2f), 2.0f, 1e9f };
            memcpy(diskUBO->contents(), diskData, sizeof(diskData));
            
            // Upload Objects
            struct ObjUBOData {
                int numObjects; float _p0, _p1, _p2;
                simd::float4 posRadius[16]; simd::float4 color[16]; float mass[16];
                float objMinR, objMaxR, _q0, _q1;
            } objData;
            objData.numObjects = std::min((int)objects.size(), 16);
            objData.objMinR = std::numeric_limits<float>::max();
            objData.objMaxR = 0.0f;
            for(int i=0; i<objData.numObjects; ++i) {
                objData.posRadius[i] = objects[i].posRadius;
                objData.color[i] = objects[i].color;
                objData.mass[i] = objects[i].mass;

                // Radial band spanned by the object, used by the shader to skip
                // the per-object intersection loop while the ray is elsewhere.
                float d = simd_length(objects[i].posRadius.xyz);
                objData.objMinR = std::min(objData.objMinR, d - objects[i].posRadius.w);
                objData.objMaxR = std::max(objData.objMaxR, d + objects[i].posRadius.w);
            }
            if (objData.numObjects == 0) { objData.objMinR = 0.0f; objData.objMaxR = 0.0f; }
            memcpy(objectsUBO->contents(), &objData, sizeof(ObjUBOData));
            
            computeEncoder->setBuffer(cameraUBO, 0, 0);
            computeEncoder->setBuffer(diskUBO, 0, 1);
            computeEncoder->setBuffer(objectsUBO, 0, 2);
            
            MTL::Size gridSize = MTL::Size::Make(COMPUTE_WIDTH, COMPUTE_HEIGHT, 1);
            MTL::Size threadGroupSize = MTL::Size::Make(16, 16, 1);
            computeEncoder->dispatchThreads(gridSize, threadGroupSize);
            computeEncoder->endEncoding();

            needsRedraw = false;
        }
        
        // Render Pass
        MTL::RenderPassDescriptor* renderPassDesc = pView->currentRenderPassDescriptor();
        if (renderPassDesc != nullptr) {
            MTL::RenderCommandEncoder* renderEncoder = cmdBuf->renderCommandEncoder(renderPassDesc);
            
            // Draw Quad
            renderEncoder->setRenderPipelineState(quadPipelineState);
            renderEncoder->setVertexBuffer(quadVBO, 0, 0);
            renderEncoder->setFragmentTexture(renderTexture, 0);
            renderEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(6));
            
            // Draw Grid
            memcpy(gridUniformsBuffer->contents(), &viewProj, sizeof(simd::float4x4));
            renderEncoder->setRenderPipelineState(gridPipelineState);
            renderEncoder->setVertexBuffer(gridVBO, 0, 0);
            renderEncoder->setVertexBuffer(gridUniformsBuffer, 0, 1);
            renderEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeLine, gridIndexCount, MTL::IndexTypeUInt32, gridEBO, 0);
            
            renderEncoder->endEncoding();
        }
        
        cmdBuf->presentDrawable(drawable);
        cmdBuf->commit();
    }
};

void scrollWheelImpl(id self, SEL cmd, id event) {
    double dy = ((double(*)(id, SEL))objc_msgSend)(event, sel_registerName("scrollingDeltaY"));
    camera.processScroll(static_cast<float>(dy));
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
        _window->setTitle(NS::String::string("Black Hole (100% Metal)", NS::UTF8StringEncoding));
        
        _device = MTL::CreateSystemDefaultDevice();
        
        static Class customViewClass = nullptr;
        if (!customViewClass) {
            Class mtkViewClass = objc_getClass("MTKView");
            customViewClass = objc_allocateClassPair(mtkViewClass, "CustomMTKView", 0);
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
    NS::Application* app = NS::Application::sharedApplication();
    app->setActivationPolicy(NS::ActivationPolicyRegular);
    app->activateIgnoringOtherApps(true);
    AppDelegate delegate;
    app->setDelegate(&delegate);
    app->run();
    return 0;
}
