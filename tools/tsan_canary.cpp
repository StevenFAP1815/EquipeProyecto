#include <iostream>
#include <thread>

int sharedCounter = 0;

int main() {
    std::thread a([] {
        for (int i = 0; i < 100000; ++i) {
            ++sharedCounter;
        }
    });
    std::thread b([] {
        for (int i = 0; i < 100000; ++i) {
            ++sharedCounter;
        }
    });
    a.join();
    b.join();
    std::cout << sharedCounter << "\n";
    return 0;
}
