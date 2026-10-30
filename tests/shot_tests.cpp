#include "shot.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        // Resolve's Edit host labels individual playback requests noninteractive.
        // They must retain the applied shot, even when random-access decoding varies.
        require(!ck::validateSequenceInputs(true, false, 17, 17),
                "Edit single-frame requests must reuse the applied shot");
        require(!ck::validateSequenceInputs(true, true, 17, 17),
                "Interactive Edit requests must reuse the applied shot");
        require(ck::validateSequenceInputs(true, false, 0, 239),
                "A full Edit render sequence must validate all dependencies once");
        require(ck::validateSequenceInputs(false, false, 17, 17),
                "Fusion noninteractive sequence validation must be preserved");
        require(!ck::validateSequenceInputs(false, true, 17, 17),
                "Fusion interactive playback must retain its applied shot");
        require(ck::validateSequenceInputs(false, true, 0, 239),
                "Multi-frame sequences must validate the entire dependency set");
        ck::AppliedState state;
        const ck::Json initial{{"settings", {{"output_mode", 0}}}, {"temporal", true}};
        auto first = state.snapshot(initial);
        auto pending = initial;
        pending["settings"]["output_mode"] = 2;
        require(state.snapshot(pending).values == initial, "Pending settings must not affect rendering");
        auto applied = state.apply(pending);
        require(applied.generation == first.generation + 1, "Apply increments generation once");
        require(state.snapshot(initial).values == pending, "Only applied settings drive rendering");
        ck::AppliedState restored;
        restored.restore(ck::Json::parse(applied.values.dump()));
        require(restored.snapshot(initial).values == pending, "Committed settings survive reload");
        state.restore(initial);
        require(state.snapshot(pending).values == initial, "Undo restores committed settings");
        require(ck::digest({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                "SHA-256 must match the worker format");
        ck::JobDirectory tmp(std::filesystem::temp_directory_path() / "ck-shot-test");
        std::vector<float> pixels{.1f, .2f, .3f, .5f};
        ck::writeFrame(tmp.path / "000000.rgba", pixels);
        std::ofstream(tmp.path / "complete.json") << ck::Json{
            {"shape", {{"width", 1}, {"height", 1}, {"frames", 1}}},
            {"digests", {ck::digest(pixels)}}};
        ck::PreparedShot shot(tmp.path, {0,0,1,1}, 0, 1, 1, first.generation,
                              {ck::digest(pixels)}, {});
        require(shot.matches(first.generation, {0,0,1,1}, 0, 1, 1, ck::digest(pixels), ""),
                "Unchanged frame must hit completed shot");
        require(!shot.matches(applied.generation, {0,0,1,1}, 0, 1, 1, ck::digest(pixels), ""),
                "Apply must invalidate native output view");
        require(!shot.matches(first.generation, {0,0,1,1}, 0, .5, .5, ck::digest(pixels), ""),
                "Render scale must be part of identity");
        require(!shot.matches(first.generation, {0,0,1,1}, 0, 1, 1, "changed input", ""),
                "Upstream pixel changes must invalidate the shot");
        require(shot.matchesSnapshot(first.generation, {0,0,1,1}, 0, 1, 1),
                "Interactive playback must reuse the committed snapshot independent of live source requests");
        require(!shot.matchesSnapshot(applied.generation, {0,0,1,1}, 0, 1, 1),
                "Refresh must invalidate the interactive snapshot");
        require(shot.read(0) == pixels, "Playback reads the requested prepared frame");
        // A non-requested frame change must invalidate before a sequence emits its first output.
        ck::writeFrame(tmp.path / "000001.rgba", pixels);
        std::ofstream(tmp.path / "complete.json") << ck::Json{
            {"shape", {{"width", 1}, {"height", 1}, {"frames", 2}}},
            {"digests", {ck::digest(pixels), ck::digest(pixels)}}};
        ck::PreparedShot sequence(tmp.path, {0,0,1,1}, 0, 1, 1, first.generation,
                                  {"source-0", "source-1"}, {"hint-0", "hint-1"});
        require(!sequence.validateInputs([](double time) {
            return std::make_pair(time == 0 ? "source-0" : "changed-source-1",
                                  time == 0 ? "hint-0" : "hint-1");
        }), "A changed future frame must invalidate before sequence output starts");
        pixels[0] = 100;
        ck::writeFrame(tmp.path / "000000.rgba", pixels);
        bool rejected = false;
        try { shot.read(0); } catch (const std::exception&) { rejected = true; }
        require(rejected, "Same-size corrupted frame must never reach the host");
        std::cout << "Applied state and shot cache contracts passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
