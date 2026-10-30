#include "bridge.hpp"
#include "image.hpp"
#include "host.hpp"
#include "parameters.hpp"
#include "shot.hpp"
#include "ofxProgress.h"
#include <chrono>
#include "ofxsImageEffect.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

namespace {
ck::Worker worker;
std::filesystem::path resources() {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&resources), &info) || !info.dli_fname)
        throw std::runtime_error("Cannot locate plugin resources");
    return std::filesystem::canonical(info.dli_fname).parent_path().parent_path() / "Resources";
}
ck::Runtime runtime(const std::string& folder) {
    std::ifstream stream(resources() / "runtime.json");
    if (!stream) throw std::runtime_error("Runtime is not configured. Run scripts/install.sh.");
    const auto config = ck::Json::parse(stream);
    std::string cache = folder;
    if (cache.empty()) {
        const char* xdg = std::getenv("XDG_CACHE_HOME");
        const char* home = std::getenv("HOME");
        if (!xdg && !home) throw std::runtime_error("HOME or Cache Folder must be set");
        cache = std::string(xdg ? xdg : home) + (xdg ? "" : "/.cache") + "/corridorkey-resolve";
    }
    return {config.at("python"), (resources() / "worker/server.py").string(), config.at("model"), cache};
}
ck::Rect rect(OfxRectI r) { return {r.x1, r.y1, r.x2, r.y2}; }
ck::ImageView view(OFX::Image& image) {
    if (image.getPixelDepth() != OFX::eBitDepthFloat)
        throw std::runtime_error("CorridorKey requires 32-bit float images");
    const int channels = image.getPixelComponents() == OFX::ePixelComponentRGBA ? 4 :
                         image.getPixelComponents() == OFX::ePixelComponentAlpha ? 1 : 0;
    return {static_cast<float*>(image.getPixelData()), rect(image.getBounds()), image.getRowBytes(), channels};
}
std::string filename(const char* prefix, int index) {
    std::ostringstream stream;
    stream << prefix << '_' << std::setw(6) << std::setfill('0') << index << ".rgba";
    return stream.str();
}

class HostProgress {
public:
    HostProgress(OfxImageEffectHandle effect, const std::string& cache) : effect_(effect), cache_(cache) {
        v2_ = static_cast<const OfxProgressSuiteV2*>(OFX::fetchSuite(kOfxProgressSuite, 2, true));
        if (!v2_) v1_ = static_cast<const OfxProgressSuiteV1*>(OFX::fetchSuite(kOfxProgressSuite, 1, true));
        const auto result = v2_ ? v2_->progressStart(effect_, "Preparing CorridorKey shot cache", "corridorkey.cache") :
            v1_ ? v1_->progressStart(effect_, "Preparing CorridorKey shot cache") : kOfxStatErrUnsupported;
        active_ = result == kOfxStatOK;
        ck::diagnostic(cache_, "progress_start", {{"suite", v2_ ? 2 : v1_ ? 1 : 0}, {"result", result}});
    }
    ~HostProgress() {
        if (active_) { if (v2_) v2_->progressEnd(effect_); else v1_->progressEnd(effect_); }
    }
    void update(double fraction, const std::string& message) {
        const auto result = !active_ ? kOfxStatErrUnsupported : v2_ ?
            v2_->progressUpdate(effect_, fraction) : v1_->progressUpdate(effect_, fraction);
        ck::diagnostic(cache_, "progress", {{"fraction", fraction}, {"message", message}, {"result", result}});
        if (result == kOfxStatReplyNo) throw std::runtime_error("CorridorKey processing cancelled");
    }
private:
    OfxImageEffectHandle effect_;
    std::string cache_;
    const OfxProgressSuiteV2* v2_ = nullptr;
    const OfxProgressSuiteV1* v1_ = nullptr;
    bool active_ = false;
};

class CorridorKey : public OFX::ImageEffect {
public:
    explicit CorridorKey(OfxImageEffectHandle handle) : OFX::ImageEffect(handle) {
        source_ = fetchClip("Source"); output_ = fetchClip("Output");
        hint_ = getContext() == OFX::eContextGeneral ? fetchClip("AlphaHint") : nullptr;
        restore();
    }
    void render(const OFX::RenderArguments& args) override;
    void beginSequenceRender(const OFX::BeginSequenceRenderArguments& args) override {
        if (ck::validateSequenceInputs(getContext() == OFX::eContextFilter, args.isInteractive,
                                      args.frameRange.min, args.frameRange.max)) validateSequence_ = true;
        const auto applied = state_.peek(ck::readSnapshot(*this, args.frameRange.min));
        ck::diagnostic(runtime(applied.values.at("cache_folder")).cache, "sequence_begin",
            {{"interactive", args.isInteractive}, {"interactive_status", args.interactiveRenderStatus},
             {"first", args.frameRange.min}, {"last", args.frameRange.max}});
    }
    void changedParam(const OFX::InstanceChangedArgs&, const std::string& name) override;
    void changedClip(const OFX::InstanceChangedArgs& args, const std::string& name) override {
        if ((name == "Source" || name == "AlphaHint") && args.reason != OFX::eChangeTime) state_.invalidate();
    }
    void syncPrivateData() override {
        const auto value = state_.peek(nullptr).values;
        if (!value.is_null()) fetchStringParam("applied_settings")->setValue(value.dump());
    }
    void getFramesNeeded(const OFX::FramesNeededArguments& args, OFX::FramesNeededSetter& setter) override;
    void getClipPreferences(OFX::ClipPreferencesSetter& setter) override {
        const auto applied = state_.peek(ck::readSnapshot(*this, 0));
        const int mode = applied.values.at("settings").at("output_mode");
        setter.setOutputPremultiplication(mode == 1 ? OFX::eImageUnPreMultiplied : OFX::eImagePreMultiplied);
    }
    bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments& args, OfxRectD& rod) override {
        rod = source_->getRegionOfDefinition(args.time); return true;
    }
private:
    bool hasHint() const { return hint_ && hint_->isConnected(); }
    void status(const std::string& text) { std::lock_guard<std::mutex> lock(statusMutex_); status_ = text; }
    std::string status() { std::lock_guard<std::mutex> lock(statusMutex_); return status_; }
    void restore() {
        std::string saved; fetchStringParam("applied_settings")->getValue(saved);
        if (!saved.empty()) state_.restore(ck::Json::parse(saved));
    }
    OfxRangeD range(double time, const ck::Json& applied);
    void collect(const OFX::RenderArguments& args, const ck::Json& cfg, OfxRangeD frames,
                 ck::Rect bounds, const std::filesystem::path& directory, std::uint64_t generation,
                 std::vector<std::string>& sources, std::vector<std::string>& hints, HostProgress& progress);
    OFX::Clip *source_, *output_, *hint_;
    ck::AppliedState state_;
    std::shared_ptr<ck::PreparedShot> prepared_;
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> validateSequence_{false};
    std::mutex statusMutex_;
    std::string status_ = "Ready - preparing the whole shot on first connection";
};

OfxRangeD CorridorKey::range(double time, const ck::Json& applied) {
    if (!applied.at("temporal").get<bool>()) return {time, time};
    OfxRangeD frames;
    if (applied.at("auto_range").get<bool>()) frames = source_->getFrameRange();
    else frames = {applied.at("shot_start").get<double>(), applied.at("shot_end").get<double>()};
    if (!std::isfinite(frames.min) || !std::isfinite(frames.max) || frames.max < frames.min ||
        frames.max - frames.min >= 10000 || std::abs(frames.min) > 1000000 || std::abs(frames.max) > 1000000)
        throw std::runtime_error("Set a finite First/Last Frame range (maximum 10,000 frames)");
    frames.min = std::ceil(frames.min); frames.max = std::floor(frames.max);
    if (time < frames.min || time > frames.max)
        throw std::runtime_error("Current frame is outside the temporal shot range");
    return frames;
}
void CorridorKey::getFramesNeeded(const OFX::FramesNeededArguments& args, OFX::FramesNeededSetter& setter) {
    try {
        const auto applied = state_.peek(ck::readSnapshot(*this, args.time));
        const auto frames = range(args.time, applied.values);
        setter.setFramesNeeded(*source_, frames);
        if (hasHint()) setter.setFramesNeeded(*hint_, frames);
    } catch (const std::exception&) {
        setter.setFramesNeeded(*source_, {args.time, args.time});
    }
}
void CorridorKey::collect(const OFX::RenderArguments& args, const ck::Json& cfg, OfxRangeD frames,
                         ck::Rect bounds, const std::filesystem::path& directory, std::uint64_t generation,
                         std::vector<std::string>& sources, std::vector<std::string>& hints, HostProgress& progress) {
    const int count = static_cast<int>(std::round(frames.max - frames.min)) + 1;
    const bool externalHint = cfg.at("hint_source") == 2;
    if (externalHint && !hasHint())
        throw std::runtime_error("Connect the Alpha Hint input in Fusion, or choose Source Alpha / None");
    for (int i = 0; i < count; ++i) {
        if (abort() || cancelled_ || state_.generation() != generation)
            throw std::runtime_error("CorridorKey processing cancelled");
        const double time = count == 1 ? args.time : frames.min + i;
        std::unique_ptr<OFX::Image> input(source_->fetchImage(time));
        if (!input) throw std::runtime_error("Source frame unavailable; check the shot range");
        const auto image = view(*input);
        if (image.bounds.x1 != bounds.x1 || image.bounds.y1 != bounds.y1 ||
            image.bounds.x2 != bounds.x2 || image.bounds.y2 != bounds.y2)
            throw std::runtime_error("Source dimensions change within the shot; resize upstream");
        const auto pixels = ck::pack(image, bounds);
        ck::writeFrame(directory / filename("source", i), pixels);
        sources.push_back(ck::digest(pixels));
        if (externalHint && !(cfg.at("hint_frames") == 1 && i > 0)) {
            std::unique_ptr<OFX::Image> hint(hint_->fetchImage(time));
            if (!hint) throw std::runtime_error("Alpha Hint frame is unavailable");
            const auto matte = ck::pack(view(*hint), bounds);
            ck::writeFrame(directory / filename("hint", i), matte);
            hints.push_back(ck::digest(matte));
        }
        progress.update(.1 * (i + 1) / count, "Collecting frame " + std::to_string(i + 1) + " / " + std::to_string(count));
    }
}
void CorridorKey::render(const OFX::RenderArguments& args) {
    cancelled_ = false;
    const auto began = std::chrono::steady_clock::now();
    std::string cache;
    try {
        std::unique_ptr<OFX::Image> input(source_->fetchImage(args.time));
        std::unique_ptr<OFX::Image> output(output_->fetchImage(args.time));
        if (!input || !output) throw std::runtime_error("Source or output image unavailable");
        const auto bounds = rect(input->getBounds());
        const auto applied = state_.snapshot(ck::readSnapshot(*this, args.time));
        try { setParamSetNeedsSyncing(); } catch (const OFX::Exception::Suite&) { /* optional in older hosts */ }
        auto cfg = applied.values.at("settings");
        if (cfg.at("output_mode") == 5) {
            ck::unpack(ck::pack(view(*input), bounds), bounds, view(*output), rect(args.renderWindow));
            return;
        }
        if (cfg.at("hint_source") == 2 && !hasHint())
            throw std::runtime_error("Connect the Alpha Hint input in Fusion, or choose Source Alpha / None");
        const auto frames = range(args.time, applied.values);
        const int count = static_cast<int>(std::round(frames.max - frames.min)) + 1;
        const auto config = runtime(applied.values.at("cache_folder"));
        cache = config.cache;
        std::filesystem::create_directories(cache);
        // Playback reads the committed shot. Refresh recaptures upstream edits;
        // final sequences validate all inputs before emitting their first frame.
        bool hit = prepared_ && prepared_->matchesSnapshot(applied.generation, bounds, args.time,
            args.renderScale.x, args.renderScale.y) && prepared_->frames() == count &&
            prepared_->firstFrame() == frames.min;
        // Final exports must validate the entire dependency set before emitting their
        // first frame; a later changed source frame must not split the export across views.
        if (validateSequence_.exchange(false) && hit) {
            hit = prepared_->validateInputs([&](double time) {
                if (abort() || cancelled_) throw std::runtime_error("CorridorKey processing cancelled");
                std::unique_ptr<OFX::Image> source(source_->fetchImage(time));
                if (!source) throw std::runtime_error("Source unavailable while validating export");
                std::string matte;
                if (cfg.at("hint_source") == 2) {
                    std::unique_ptr<OFX::Image> hint(hint_->fetchImage(cfg.at("hint_frames") == 1 ? frames.min : time));
                    if (!hint) throw std::runtime_error("Hint unavailable while validating export");
                    matte = ck::digest(ck::pack(view(*hint), bounds));
                }
                return std::make_pair(ck::digest(ck::pack(view(*source), bounds)), matte);
            });
            ck::diagnostic(cache, "sequence_validated", {{"valid", hit}, {"frames", count}});
        }
        std::vector<float> pixels;
        if (hit) {
            try { pixels = prepared_->read(args.time); }
            catch (const std::exception& error) {
                ck::diagnostic(cache, "cache_repair", {{"error", error.what()}}); hit = false;
            }
        }
        if (!hit) {
            prepared_.reset();
            ck::diagnostic(cache, "prepare_begin", {{"frames", count}, {"first", frames.min},
                {"last", frames.max}, {"time", args.time}, {"generation", applied.generation},
                {"width", bounds.x2 - bounds.x1}, {"height", bounds.y2 - bounds.y1},
                {"scale_x", args.renderScale.x}, {"scale_y", args.renderScale.y},
                {"draft", args.renderQualityDraft}, {"sequential", args.sequentialRenderStatus}});
            HostProgress progress(getHandle(), cache);
            const auto needed = static_cast<std::uintmax_t>(bounds.x2 - bounds.x1) *
                (bounds.y2 - bounds.y1) * count * (cfg.at("hint_source") == 2 ? 32 : 16);
            if (std::filesystem::space(cache).available < needed + 512 * 1024 * 1024)
                throw std::runtime_error("Not enough disk space to collect this shot. Shorten the range or change Cache Folder.");
            ck::JobDirectory directory(cache);
            std::vector<std::string> sources, hints;
            status("Collecting " + std::to_string(count) + " frames for the shot cache");
            collect(args, cfg, frames, bounds, directory.path, applied.generation, sources, hints, progress);
            cfg["fps"] = ck::frameRateOrDefault([this] { return source_->getFrameRate(); });
            if (count == 1) { cfg["hann_chunk"] = 1; cfg["hann_stride"] = 1; }
            ck::Json job{{"directory", directory.path.string()}, {"width", bounds.x2 - bounds.x1},
                {"height", bounds.y2 - bounds.y1}, {"frames", count},
                {"target", static_cast<int>(std::round(args.time - frames.min))}, {"settings", cfg}};
            auto result = worker.request(config, job, [this, applied] {
                return abort() || cancelled_.load() || state_.generation() != applied.generation;
            }, 7200, [&](double fraction, const std::string& message) {
                status(message); progress.update(.1 + .9 * fraction, message);
            });
            if (state_.generation() != applied.generation) throw std::runtime_error("Settings changed during preparation");
            prepared_ = std::make_shared<ck::PreparedShot>(result.at("view_directory").get<std::string>(), bounds,
                frames.min, args.renderScale.x, args.renderScale.y, applied.generation, std::move(sources), std::move(hints));
            pixels = prepared_->read(args.time);
            status("Ready - " + std::to_string(count) + " frames cached. Edits wait for Apply / Refresh.");
            ck::diagnostic(cache, "prepare_complete", {{"frames", count}, {"neural_cached", result.at("cached")},
                {"worker_seconds", result.at("seconds")}, {"view_directory", result.at("view_directory")}});
        }
        ck::unpack(pixels, bounds, view(*output), rect(args.renderWindow));
        const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        ck::diagnostic(cache, hit ? "cache_hit" : "render_ready", {{"frame", args.time}, {"milliseconds", milliseconds},
            {"generation", applied.generation}, {"source_id", input->getUniqueIdentifier()}});
        ck::notifyHostIfSupported([this] { clearPersistentMessage(); });
    } catch (const std::exception& error) {
        status(error.what());
        if (!cache.empty()) ck::diagnostic(cache, "render_error", {{"error", error.what()}, {"frame", args.time}});
        ck::notifyHostIfSupported([&] {
            setPersistentMessage(OFX::Message::eMessageError, "corridorkey", error.what());
        });
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
}
void CorridorKey::changedParam(const OFX::InstanceChangedArgs& args, const std::string& name) {
    if (name == "applied_settings") { restore(); return; }
    if (name == "refresh_status") fetchStringParam("status")->setValue(status());
    if (name == "cancel") cancelled_ = true;
    if (name == "release_gpu") {
        cancelled_ = true; worker.stop();
        status("GPU released - prepared frames remain available");
        fetchStringParam("status")->setValue(status());
    }
    if (name == "apply" || name == "process_shot") {
        syncPrivateData(); // Give Undo a persistent copy of the previous applied state.
        beginEditBlock("Apply CorridorKey settings");
        try {
            if (name == "process_shot") fetchBooleanParam("temporal")->setValue(true);
            const auto applied = state_.apply(ck::readSnapshot(*this, args.time));
            fetchStringParam("applied_settings")->setValue(applied.values.dump());
            int revision; fetchIntParam("applied_revision")->getValue(revision);
            fetchIntParam("applied_revision")->setValue(revision == 1000000 ? 0 : revision + 1);
            status("Preparing applied settings...");
            fetchStringParam("status")->setValue(status());
            endEditBlock();
        } catch (...) { endEditBlock(); throw; }
    }
}
class Factory : public OFX::PluginFactoryHelper<Factory> {
public:
    Factory() : OFX::PluginFactoryHelper<Factory>("org.corridorkey.community.resolve.v2", 0, 2) {}
    void load() override {}
    void unload() override { worker.stop(); }
    void describe(OFX::ImageEffectDescriptor& desc) override {
        desc.setLabels("CorridorKey 2.0", "CorridorKey 2.0", "CorridorKey 2.0");
        desc.setPluginGrouping("Keying");
        desc.setPluginDescription("Local CorridorKey 2.0 neural keyer with optional alpha hints and temporal processing.");
        desc.addSupportedContext(OFX::eContextGeneral); desc.addSupportedContext(OFX::eContextFilter);
        desc.addSupportedBitDepth(OFX::eBitDepthFloat);
        desc.setSingleInstance(false); desc.setHostFrameThreading(false);
        desc.setSupportsTiles(false); desc.setSupportsMultiResolution(false);
        desc.setSupportsMultipleClipPARs(false); desc.setTemporalClipAccess(true);
        desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
    }
    void describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context) override {
        for (const auto* name : {"Source", "AlphaHint", "Output"}) {
            if (std::string(name) == "AlphaHint" && context != OFX::eContextGeneral) continue;
            auto* clip = desc.defineClip(name);
            clip->addSupportedComponent(OFX::ePixelComponentRGBA);
            clip->setSupportsTiles(false);
            const bool hint = std::string(name) == "AlphaHint";
            if (hint) { clip->addSupportedComponent(OFX::ePixelComponentAlpha); clip->setOptional(true); }
            clip->setIsMask(false); clip->setTemporalClipAccess(std::string(name) != "Output");
            clip->setLabel(hint ? "Alpha Hint" : name);
        }
        ck::defineParameters(desc);
    }
    OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum) override {
        return new CorridorKey(handle);
    }
};
}
void OFX::Plugin::getPluginIDs(OFX::PluginFactoryArray& factories) {
    static Factory factory; factories.push_back(&factory);
}
