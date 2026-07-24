#include <objc/runtime.h>
#include <objc/message.h>
#include <iostream>

void scrollWheelImpl(id self, SEL cmd, id event) {
    float dy = ((float(*)(id, SEL))objc_msgSend)(event, sel_registerName("scrollingDeltaY"));
    std::cout << "scrolled: " << dy << std::endl;
}

int main() {
    Class nsobjectClass = objc_getClass("NSObject");
    Class customClass = objc_allocateClassPair(nsobjectClass, "CustomClass", 0);
    class_addMethod(customClass, sel_registerName("scrollWheel:"), (IMP)scrollWheelImpl, "v@:@");
    objc_registerClassPair(customClass);
    std::cout << "Success!" << std::endl;
    return 0;
}
