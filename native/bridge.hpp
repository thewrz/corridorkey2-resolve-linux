#pragma once
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

namespace ck {
using Json = nlohmann::json;
struct Runtime {
    std::string python, server, model, cache;
    bool operator==(const Runtime& b) const {
        return python == b.python && server == b.server && model == b.model && cache == b.cache;
    }
};
class Worker {
public:
    ~Worker();
    Json request(const Runtime& runtime, const Json& job, const std::function<bool()>& abort,
                 int timeoutSeconds = 1800,
                 const std::function<void(double, const std::string&)>& progress = {});
    void stop();
private:
    void start(const Runtime& runtime);
    void stopUnlocked();
    std::mutex mutex_;
    Runtime runtime_;
    int socket_ = -1;
    int pid_ = -1;
};
class JobDirectory {
public:
    explicit JobDirectory(const std::filesystem::path& cache);
    ~JobDirectory();
    std::filesystem::path path;
};
void writeFrame(const std::filesystem::path& path, const std::vector<float>& pixels);
std::vector<float> readFrame(const std::filesystem::path& path, std::size_t count);
}
