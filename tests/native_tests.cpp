#include "image.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

int main() {
    try {
        // Bottom-up OFX image with nonzero origin and padded scanlines.
        std::vector<float> pixels(24, -9);
        pixels[0] = 1; pixels[4] = 2; pixels[12] = 3; pixels[16] = 4;
        ck::Rect bounds{10, 20, 12, 22};
        auto packed = ck::pack({pixels.data(), bounds, 48, 4}, bounds);
        require(packed.size() == 16 && packed[0] == 3 && packed[4] == 4
                && packed[8] == 1 && packed[12] == 2, "OFX vertical orientation");
        auto negative = ck::pack({pixels.data() + 12, bounds, -48, 4}, bounds);
        require(negative[0] == 1 && negative[8] == 3, "negative row bytes");
        std::vector<float> output(24, 99);
        ck::unpack(packed, bounds, {output.data(), bounds, 48, 4}, {11, 20, 12, 22});
        require(output[0] == 99 && output[4] == 2 && output[16] == 4,
                "only requested ROI is written");
        float alpha[] = {0.2f, 0.8f};
        auto mask = ck::pack({alpha, {10, 20, 12, 21}, 8, 1}, bounds);
        require(mask[0] == 0 && std::abs(mask[11] - 0.2f) < 1e-6,
                "alpha-only and out-of-bounds mask pixels");
        bool rejected = false;
        try { ck::pack({pixels.data(), bounds, 4, 4}, bounds); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "invalid stride rejected");
        require(ck::frameRateOrDefault([] { return 29.97; }) == 29.97, "valid frame rate preserved");
        require(ck::frameRateOrDefault([]() -> double {
            throw std::runtime_error("OfxImageEffectPropFrameRate");
        }) == 24.0, "Fusion clips without optional frame rate use metadata default");
        std::cout << "Native image contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
