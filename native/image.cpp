#include "image.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
namespace ck {
double frameRateOrDefault(const std::function<double()>& read) {
    // Fusion 21 omits this optional clip property. FPS only labels the internal
    // image sequence; host sample times, not this fallback, drive temporal reads.
    try {
        const double value = read();
        if (std::isfinite(value) && value > 0 && value <= 1000) return value;
    } catch (const std::exception&) {
        return 24.0;
    }
    return 24.0;
}
namespace {
std::size_t count(Rect r) {
    const auto w = static_cast<std::int64_t>(r.x2) - r.x1;
    const auto h = static_cast<std::int64_t>(r.y2) - r.y1;
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || w * h > 134217728)
        throw std::runtime_error("Invalid image dimensions");
    return static_cast<std::size_t>(w * h * 4);
}
void validate(const ImageView& image) {
    count(image.bounds);
    if (!image.data || (image.channels != 1 && image.channels != 4))
        throw std::runtime_error("Expected float RGBA or alpha pixels");
    if (std::abs(image.rowBytes) < (image.bounds.x2 - image.bounds.x1) * image.channels * 4)
        throw std::runtime_error("Invalid image row stride");
}
float* pixel(const ImageView& image, int x, int y) {
    if (x < image.bounds.x1 || y < image.bounds.y1 || x >= image.bounds.x2 || y >= image.bounds.y2)
        return nullptr;
    auto row = reinterpret_cast<char*>(image.data) + (y - image.bounds.y1) * image.rowBytes;
    return reinterpret_cast<float*>(row) + (x - image.bounds.x1) * image.channels;
}
}
std::vector<float> pack(const ImageView& image, Rect region) {
    validate(image);
    std::vector<float> result(count(region), 0);
    std::size_t offset = 0;
    for (int y = region.y2 - 1; y >= region.y1; --y) {
        for (int x = region.x1; x < region.x2; ++x, offset += 4) {
            const auto* input = pixel(image, x, y);
            if (!input) continue;
            if (image.channels == 4) std::copy_n(input, 4, result.data() + offset);
            else std::fill_n(result.data() + offset, 4, *input);
        }
    }
    return result;
}
void unpack(const std::vector<float>& pixels, Rect region, ImageView output, Rect window) {
    validate(output);
    if (pixels.size() != count(region)) throw std::runtime_error("Incomplete inference result");
    for (int y = std::max(window.y1, output.bounds.y1); y < std::min(window.y2, output.bounds.y2); ++y) {
        for (int x = std::max(window.x1, output.bounds.x1); x < std::min(window.x2, output.bounds.x2); ++x) {
            auto* target = pixel(output, x, y);
            if (x < region.x1 || x >= region.x2 || y < region.y1 || y >= region.y2) {
                std::fill_n(target, output.channels, 0);
                continue;
            }
            const auto index = (static_cast<std::size_t>(region.y2 - y - 1) *
                                (region.x2 - region.x1) + x - region.x1) * 4;
            if (output.channels == 4) std::copy_n(pixels.data() + index, 4, target);
            else *target = pixels[index + 3];
        }
    }
}
}
