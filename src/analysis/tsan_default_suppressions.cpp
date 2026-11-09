extern "C" const char* __tsan_default_suppressions() {
    return "race:libQt5Core.so\n"
           "race:libdbus-1.so\n"
           "deadlock:libdbus-1.so\n";
}
