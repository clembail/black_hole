#include <iostream>
#include <AppKit/AppKit.h>
int main() {
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel handler:^NSEvent *(NSEvent *event) {
        std::cout << "Scroll: " << [event deltaY] << std::endl;
        return event;
    }];
    return 0;
}
