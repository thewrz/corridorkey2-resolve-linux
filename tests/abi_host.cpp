#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

// Resolve's libProResRAW exports an incompatible implementation of this symbol.
// Reproduce interposition deterministically without depending on that library.
extern "C" void host_filesystem() asm("_ZNSt10filesystem7__cxx114path14_M_split_cmptsEv");
extern "C" void host_filesystem() {
    std::fputs("Host C++ runtime overrode the plugin's filesystem implementation\n", stderr);
    std::_Exit(42);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    auto* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) { std::fputs(dlerror(), stderr); return 3; }
    auto probe = reinterpret_cast<int (*)()>(dlsym(module, "ck_runtime_probe"));
    const int result = probe && probe() == 1 ? 0 : 4;
    dlclose(module);
    return result;
}
