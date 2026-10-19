#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <iostream>

int main() {
    auto pool = NS::AutoreleasePool::alloc()->init();
    NS::Bundle* mainBundle = NS::Bundle::mainBundle();
    if (mainBundle) {
        NS::String* path = mainBundle->resourcePath();
        if (path) {
            std::cout << "Resource path: " << path->utf8String() << std::endl;
        } else {
            std::cout << "No resource path" << std::endl;
        }
        NS::String* bundlePath = mainBundle->bundlePath();
        if (bundlePath) {
            std::cout << "Bundle path: " << bundlePath->utf8String() << std::endl;
        }
    }
    pool->release();
    return 0;
}
