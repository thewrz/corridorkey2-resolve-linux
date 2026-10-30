#include <filesystem>

extern "C" __attribute__((visibility("default"))) int ck_runtime_probe() {
    const auto path = std::filesystem::path("/tmp") / "corridorkey-probe";
    return path.filename() == "corridorkey-probe" && path.parent_path() == "/tmp";
}
