#include "bridge.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;
namespace ck {
Worker::~Worker() { stop(); }
void Worker::stop() { std::lock_guard<std::mutex> lock(mutex_); stopUnlocked(); }
void Worker::stopUnlocked() {
    if (socket_ >= 0) { close(socket_); socket_ = -1; }
    if (pid_ < 0) return;
    kill(pid_, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        if (waitpid(pid_, nullptr, WNOHANG) != 0) { pid_ = -1; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    kill(pid_, SIGKILL);
    while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
    pid_ = -1;
}
void Worker::start(const Runtime& runtime) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0)
        throw std::runtime_error("Cannot create inference pipe");
    std::filesystem::create_directories(runtime.cache);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pair[1], STDOUT_FILENO);
    const std::string log = runtime.cache + "/worker.log";
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, log.c_str(),
                                   O_CREAT | O_WRONLY | O_APPEND, 0600);
    std::vector<std::string> args{runtime.python, "-u", runtime.server,
        "--model-root", runtime.model, "--cache-root", runtime.cache};
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    // Resolve ships private libraries that must not leak into Python/CUDA.
    std::vector<std::string> env;
    for (char** item = environ; *item; ++item) {
        const std::string value(*item);
        if (value.rfind("LD_LIBRARY_PATH=", 0) == 0 || value.rfind("LD_PRELOAD=", 0) == 0 ||
            value.rfind("PYTHON", 0) == 0 || value.rfind("QT_", 0) == 0) continue;
        env.push_back(value);
    }
    env.emplace_back("PYTHONNOUSERSITE=1");
    std::vector<char*> envp;
    for (auto& value : env) envp.push_back(value.data());
    envp.push_back(nullptr);
    pid_t child = -1;
    int status = posix_spawn(&child, runtime.python.c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    close(pair[1]);
    if (status) { close(pair[0]); throw std::runtime_error("Cannot start Python: " + std::string(strerror(status))); }
    socket_ = pair[0]; pid_ = child; runtime_ = runtime;
}
Json Worker::request(const Runtime& runtime, const Json& job, const std::function<bool()>& abort,
                     int timeoutSeconds,
                     const std::function<void(double, const std::string&)>& progress) {
    std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    while (!lock.try_lock()) {
        if (abort() || std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Cancelled while waiting for inference");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    try {
        if (pid_ >= 0 && !(runtime == runtime_)) stopUnlocked();
        if (pid_ < 0) start(runtime);
        const std::string message = job.dump() + "\n";
        if (message.size() > 65536) throw std::runtime_error("Inference request too large");
        std::size_t sent = 0;
        while (sent < message.size()) {
            const auto n = send(socket_, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("Inference worker closed its input");
            sent += static_cast<std::size_t>(n);
        }
        std::string reply;
        while (true) {
            if (abort()) throw std::runtime_error("CorridorKey processing cancelled");
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Inference timed out; shorten the shot or lower Internal Resolution");
            const auto newline = reply.find('\n');
            if (newline != std::string::npos) {
                if (newline > 65536) throw std::runtime_error("Invalid inference response");
                auto response = Json::parse(reply.substr(0, newline));
                reply.erase(0, newline + 1);
                if (response.value("event", "") == "progress") {
                    const double fraction = response.at("fraction");
                    if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
                        throw std::runtime_error("Invalid inference progress");
                    if (progress) progress(fraction, response.value("message", "Processing"));
                    continue;
                }
                if (!response.value("ok", false))
                    throw std::runtime_error(response.value("error", "Inference failed"));
                return response;
            }
            if (reply.size() > 65536) throw std::runtime_error("Invalid inference response");
            pollfd fd{socket_, POLLIN, 0};
            int ready = poll(&fd, 1, 100);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) throw std::runtime_error("Cannot read inference pipe");
            if (!ready) continue;
            char buffer[4096];
            const auto n = recv(socket_, buffer, sizeof(buffer), 0);
            if (n <= 0) throw std::runtime_error("Inference worker stopped. See Cache Folder/worker.log");
            reply.append(buffer, static_cast<std::size_t>(n));
        }
    } catch (...) { stopUnlocked(); throw; }
}
JobDirectory::JobDirectory(const std::filesystem::path& cache) {
    std::filesystem::create_directories(cache / "jobs");
    std::string pattern = (cache / "jobs/job-XXXXXX").string();
    char* created = mkdtemp(pattern.data());
    if (!created) throw std::runtime_error("Cannot create private cache job directory");
    path = created;
}
JobDirectory::~JobDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}
void writeFrame(const std::filesystem::path& path, const std::vector<float>& pixels) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size() * 4));
    if (!stream) throw std::runtime_error("Cannot write input frame; check cache disk space");
}
std::vector<float> readFrame(const std::filesystem::path& path, std::size_t count) {
    if (std::filesystem::file_size(path) != count * 4) throw std::runtime_error("Invalid inference output size");
    std::vector<float> result(count);
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(count * 4));
    if (!stream) throw std::runtime_error("Cannot read inference output");
    return result;
}
}
