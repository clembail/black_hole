#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>
#include <iostream>

int main() {
    NS::Application* app = NS::Application::sharedApplication();
    app->setActivationPolicy(NS::ActivationPolicyRegular);
    
    CGRect frame = (CGRect){ {100.0, 100.0}, {800.0, 600.0} };
    NS::Window* window = NS::Window::alloc()->init(
        frame,
        NS::WindowStyleMaskClosable|NS::WindowStyleMaskTitled,
        NS::BackingStoreBuffered,
        false
    );
    window->setTitle(NS::String::string("Test", NS::UTF8StringEncoding));
    window->makeKeyAndOrderFront(nullptr);
    
    app->activateIgnoringOtherApps(true);

    std::cout << "Starting loop" << std::endl;
    bool running = true;
    while(running) {
        NS::Event* event;
        while((event = app->nextEventMatchingMask(NS::EventMaskAny, NS::Date::distantPast(), NS::DefaultRunLoopMode, true))) {
            if (event->type() == NS::EventTypeAppKitDefined) {
                if (event->subtype() == NS::WindowExposedEventType) {
                    // Window closed?
                }
            }
            app->sendEvent(event);
        }
        // sleep
    }
    return 0;
}
