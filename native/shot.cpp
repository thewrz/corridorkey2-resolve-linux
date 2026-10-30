#include "shot.hpp"
#include <openssl/sha.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unistd.h>

namespace ck {
bool validateSequenceInputs(bool filterContext, bool interactive, double first, double last) {
    // Resolve's Edit Filter host also marks playback and preview frames as
    // noninteractive. Only a multi-frame sequence is an export boundary there;
    // isolated requests continue reading the committed snapshot until Refresh.
    return last > first || (!filterContext && !interactive);
}
AppliedSnapshot AppliedState::snapshot(const Json& initial) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (values_.is_null()) values_ = initial;
    return {values_, generation_};
}
AppliedSnapshot AppliedState::peek(const Json& initial) {
    std::lock_guard<std::mutex> lock(mutex_);
    return {values_.is_null() ? initial : values_, generation_};
}
AppliedSnapshot AppliedState::apply(const Json& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_ = values; return {values_, ++generation_};
}
void AppliedState::restore(const Json& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (values_ != values) { values_ = values; ++generation_; }
}
std::uint64_t AppliedState::generation() {
    std::lock_guard<std::mutex> lock(mutex_); return generation_;
}
void AppliedState::invalidate() {
    std::lock_guard<std::mutex> lock(mutex_); ++generation_;
}
std::string digest(const std::vector<float>& pixels) {
    unsigned char bytes[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(pixels.data()), pixels.size() * sizeof(float), bytes);
    const char hex[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
std::string frameName(int index) {
    std::ostringstream stream;
    stream << std::setw(6) << std::setfill('0') << index << ".rgba";
    return stream.str();
}
void diagnostic(const std::filesystem::path& cache, const std::string& event, Json details) {
    try {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        if (!details.is_object()) details = Json::object();
        details["event"] = event;
        details["pid"] = getpid();
        details["timestamp_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::ofstream stream(cache / "plugin.jsonl", std::ios::app);
        stream << details.dump() << '\n';
    } catch (...) { /* Diagnostics must not affect image evaluation. */ }
}
PreparedShot::PreparedShot(const std::filesystem::path& directory, Rect bounds, double first,
        double scaleX, double scaleY, std::uint64_t generation,
        std::vector<std::string> sources, std::vector<std::string> hints)
    : directory_(directory), bounds_(bounds), first_(first), scaleX_(scaleX), scaleY_(scaleY),
      generation_(generation), sources_(std::move(sources)), hints_(std::move(hints)) {
    const auto marker = directory / "complete.json";
    if (std::filesystem::file_size(marker) > 2 * 1024 * 1024)
        throw std::runtime_error("Invalid shot cache manifest size");
    std::ifstream stream(marker);
    const auto info = Json::parse(stream);
    const Json shape{{"width", bounds.x2 - bounds.x1}, {"height", bounds.y2 - bounds.y1},
                     {"frames", sources_.size()}};
    if (sources_.empty() || sources_.size() > 10000 || info.at("shape") != shape ||
        (!hints_.empty() && hints_.size() != 1 && hints_.size() != sources_.size()))
        throw std::runtime_error("Shot cache shape mismatch");
    digests_ = info.at("digests").get<std::vector<std::string>>();
    if (digests_.size() != sources_.size()) throw std::runtime_error("Incomplete shot cache");
    for (const auto& value : digests_)
        if (value.size() != 64 || value.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Invalid shot cache digest");
}
bool PreparedShot::matches(std::uint64_t generation, Rect bounds, double time, double scaleX,
                          double scaleY, const std::string& source, const std::string& hint) const {
    if (!matchesSnapshot(generation, bounds, time, scaleX, scaleY)) return false;
    const int index = static_cast<int>(std::round(time - first_));
    return sources_.at(index) == source && (hints_.empty() ? hint.empty() :
        hints_.at(hints_.size() == 1 ? 0 : index) == hint);
}
bool PreparedShot::matchesSnapshot(std::uint64_t generation, Rect bounds, double time,
                                  double scaleX, double scaleY) const {
    if (generation != generation_ || bounds.x1 != bounds_.x1 || bounds.y1 != bounds_.y1 ||
        bounds.x2 != bounds_.x2 || bounds.y2 != bounds_.y2 || scaleX != scaleX_ || scaleY != scaleY_ ||
        time < first_ || time > first_ + frames() - 1 || std::abs(time - first_ - std::round(time - first_)) > 1e-6)
        return false;
    return true;
}
std::vector<float> PreparedShot::read(double time) const {
    if (time < first_ || time > first_ + frames() - 1)
        throw std::runtime_error("Frame outside prepared shot");
    const int index = static_cast<int>(std::round(time - first_));
    const auto pixels = readFrame(directory_ / frameName(index),
        static_cast<std::size_t>(bounds_.x2 - bounds_.x1) * (bounds_.y2 - bounds_.y1) * 4);
    if (digest(pixels) != digests_.at(index)) throw std::runtime_error("Cached frame checksum mismatch");
    return pixels;
}
bool PreparedShot::validateInputs(const std::function<std::pair<std::string, std::string>(double)>& fetch) const {
    for (int index = 0; index < frames(); ++index) {
        const auto input = fetch(first_ + index);
        if (input.first != sources_.at(index) || input.second !=
            (hints_.empty() ? "" : hints_.at(hints_.size() == 1 ? 0 : index))) return false;
    }
    return true;
}
}
