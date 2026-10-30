#pragma once
#include <cstddef>
#include <functional>
#include <vector>

namespace ck {
struct Rect { int x1, y1, x2, y2; };
struct ImageView {
    float* data;
    Rect bounds;
    std::ptrdiff_t rowBytes;
    int channels;
};
std::vector<float> pack(const ImageView& image, Rect region);
void unpack(const std::vector<float>& pixels, Rect region, ImageView output, Rect window);
double frameRateOrDefault(const std::function<double()>& read);
}
