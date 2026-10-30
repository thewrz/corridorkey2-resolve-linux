#pragma once
#include "bridge.hpp"
#include "image.hpp"
#include <cstdint>

namespace ck {
struct AppliedSnapshot { Json values; std::uint64_t generation; };
class AppliedState {
public:
    AppliedSnapshot snapshot(const Json& initial);
    AppliedSnapshot peek(const Json& initial);
    AppliedSnapshot apply(const Json& values);
    void restore(const Json& values);
    std::uint64_t generation();
    void invalidate();
private:
    std::mutex mutex_;
    Json values_;
    std::uint64_t generation_ = 0;
};
std::string digest(const std::vector<float>& pixels);
std::string frameName(int index);
void diagnostic(const std::filesystem::path& cache, const std::string& event, Json details = {});
bool validateSequenceInputs(bool filterContext, bool interactive, double first, double last);

class PreparedShot {
public:
    PreparedShot(const std::filesystem::path& directory, Rect bounds, double first,
                 double scaleX, double scaleY, std::uint64_t generation,
                 std::vector<std::string> sources, std::vector<std::string> hints);
    bool matches(std::uint64_t generation, Rect bounds, double time, double scaleX,
                 double scaleY, const std::string& source, const std::string& hint) const;
    bool matchesSnapshot(std::uint64_t generation, Rect bounds, double time,
                         double scaleX, double scaleY) const;
    double firstFrame() const { return first_; }
    std::vector<float> read(double time) const;
    bool validateInputs(const std::function<std::pair<std::string, std::string>(double)>& fetch) const;
    int frames() const { return static_cast<int>(sources_.size()); }
private:
    std::filesystem::path directory_;
    Rect bounds_;
    double first_, scaleX_, scaleY_;
    std::uint64_t generation_;
    std::vector<std::string> sources_, hints_, digests_;
};
}
