// Exercise the built plugin's public OFX entry point, without Resolve or a GPU.
#include "ofxImageEffect.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {
using Value = std::variant<int, double, std::string, void*>;
using Properties = std::map<std::string, std::vector<Value>>;
struct Effect {
    Properties properties, paramProperties;
    std::map<std::string, Properties> clips, params;
};
Properties& props(OfxPropertySetHandle h) { return *reinterpret_cast<Properties*>(h); }
OfxPropertySetHandle handle(Properties& p) { return reinterpret_cast<OfxPropertySetHandle>(&p); }
Effect& effect(OfxImageEffectHandle h) { return *reinterpret_cast<Effect*>(h); }

template<class T> OfxStatus set(OfxPropertySetHandle h, const char* key, int i, T value) {
    if (!h || i < 0) return kOfxStatErrBadIndex;
    auto& values = props(h)[key];
    if (values.size() <= static_cast<size_t>(i)) values.resize(i + 1);
    values[i] = value;
    return kOfxStatOK;
}
OfxStatus setString(OfxPropertySetHandle h, const char* key, int i, const char* value) {
    return set(h, key, i, std::string(value));
}
template<class T> OfxStatus get(OfxPropertySetHandle h, const char* key, int i, T* value) {
    const auto found = props(h).find(key);
    if (found == props(h).end()) return kOfxStatErrUnknown;
    if (i < 0 || static_cast<size_t>(i) >= found->second.size()) return kOfxStatErrBadIndex;
    const auto* stored = std::get_if<T>(&found->second[i]);
    if (!stored) return kOfxStatErrValue;
    *value = *stored;
    return kOfxStatOK;
}
OfxStatus getString(OfxPropertySetHandle h, const char* key, int i, char** value) {
    const auto found = props(h).find(key);
    if (found == props(h).end()) return kOfxStatErrUnknown;
    if (i < 0 || static_cast<size_t>(i) >= found->second.size()) return kOfxStatErrBadIndex;
    auto* stored = std::get_if<std::string>(&found->second[i]);
    if (!stored) return kOfxStatErrValue;
    *value = stored->data();
    return kOfxStatOK;
}
template<class T> OfxStatus setN(OfxPropertySetHandle h, const char* key, int count, const T* values) {
    for (int i = 0; i < count; ++i) set(h, key, i, values[i]);
    return kOfxStatOK;
}
OfxStatus setStrings(OfxPropertySetHandle h, const char* key, int count, const char* const* values) {
    for (int i = 0; i < count; ++i) setString(h, key, i, values[i]);
    return kOfxStatOK;
}
template<class T> OfxStatus getN(OfxPropertySetHandle h, const char* key, int count, T* values) {
    for (int i = 0; i < count; ++i) {
        const auto status = get(h, key, i, &values[i]);
        if (status != kOfxStatOK) return status;
    }
    return kOfxStatOK;
}
OfxStatus getStrings(OfxPropertySetHandle h, const char* key, int count, char** values) {
    for (int i = 0; i < count; ++i) {
        const auto status = getString(h, key, i, &values[i]);
        if (status != kOfxStatOK) return status;
    }
    return kOfxStatOK;
}
OfxStatus reset(OfxPropertySetHandle h, const char* key) { props(h).erase(key); return kOfxStatOK; }
OfxStatus dimension(OfxPropertySetHandle h, const char* key, int* count) {
    const auto found = props(h).find(key);
    *count = found == props(h).end() ? 0 : static_cast<int>(found->second.size());
    return kOfxStatOK;
}
OfxPropertySuiteV1 propertySuite{set<void*>, setString, set<double>, set<int>,
    setN<void*>, setStrings, setN<double>, setN<int>, get<void*>, getString,
    get<double>, get<int>, getN<void*>, getStrings, getN<double>, getN<int>, reset, dimension};
OfxImageEffectSuiteV1 imageSuite{};
OfxParameterSuiteV1 paramSuite{};
OfxMemorySuiteV1 memorySuite{};
OfxMultiThreadSuiteV1 threadSuite{};
OfxMessageSuiteV1 messageSuite{};

const void* fetchSuite(OfxPropertySetHandle, const char* name, int version) {
    if (version != 1) return nullptr;
    if (!std::strcmp(name, kOfxPropertySuite)) return &propertySuite;
    if (!std::strcmp(name, kOfxImageEffectSuite)) return &imageSuite;
    if (!std::strcmp(name, kOfxParameterSuite)) return &paramSuite;
    if (!std::strcmp(name, kOfxMemorySuite)) return &memorySuite;
    if (!std::strcmp(name, kOfxMultiThreadSuite)) return &threadSuite;
    if (!std::strcmp(name, kOfxMessageSuite)) return &messageSuite;
    return nullptr;
}
OfxStatus paramValue(OfxParamHandle h, ...) {
    va_list args;
    va_start(args, h);
    const auto status = getString(reinterpret_cast<OfxPropertySetHandle>(h),
        kOfxParamPropDefault, 0, va_arg(args, char**));
    va_end(args);
    return status;
}
void initializeSuites() {
    imageSuite.getPropertySet = [](OfxImageEffectHandle h, OfxPropertySetHandle* p) {
        *p = handle(effect(h).properties); return kOfxStatOK;
    };
    imageSuite.getParamSet = [](OfxImageEffectHandle h, OfxParamSetHandle* p) {
        *p = reinterpret_cast<OfxParamSetHandle>(h); return kOfxStatOK;
    };
    imageSuite.clipDefine = [](OfxImageEffectHandle h, const char* name, OfxPropertySetHandle* p) {
        *p = handle(effect(h).clips[name]); return kOfxStatOK;
    };
    imageSuite.clipGetHandle = [](OfxImageEffectHandle h, const char* name,
                                 OfxImageClipHandle* clip, OfxPropertySetHandle* p) {
        auto found = effect(h).clips.find(name);
        if (found == effect(h).clips.end()) return kOfxStatErrUnknown;
        *clip = reinterpret_cast<OfxImageClipHandle>(&found->second);
        if (p) *p = handle(found->second);
        return kOfxStatOK;
    };
    imageSuite.clipGetPropertySet = [](OfxImageClipHandle h, OfxPropertySetHandle* p) {
        *p = reinterpret_cast<OfxPropertySetHandle>(h); return kOfxStatOK;
    };
    paramSuite.paramSetGetPropertySet = [](OfxParamSetHandle h, OfxPropertySetHandle* p) {
        *p = handle(reinterpret_cast<Effect*>(h)->paramProperties); return kOfxStatOK;
    };
    paramSuite.paramDefine = [](OfxParamSetHandle h, const char* type, const char* name,
                               OfxPropertySetHandle* p) {
        *p = handle(reinterpret_cast<Effect*>(h)->params[name]);
        setString(*p, kOfxParamPropType, 0, type);
        return kOfxStatOK;
    };
    paramSuite.paramGetHandle = [](OfxParamSetHandle h, const char* name,
                                  OfxParamHandle* param, OfxPropertySetHandle* p) {
        auto& params = reinterpret_cast<Effect*>(h)->params;
        auto found = params.find(name);
        if (found == params.end()) return kOfxStatErrUnknown;
        *param = reinterpret_cast<OfxParamHandle>(&found->second);
        if (p) *p = handle(found->second);
        return kOfxStatOK;
    };
    paramSuite.paramGetPropertySet = [](OfxParamHandle h, OfxPropertySetHandle* p) {
        *p = reinterpret_cast<OfxPropertySetHandle>(h); return kOfxStatOK;
    };
    paramSuite.paramGetValue = paramValue;
}
Properties hostProperties() {
    Properties p;
    setString(handle(p), kOfxPropName, 0, "org.corridorkey.contract-host");
    setString(handle(p), kOfxPropLabel, 0, "Contract host");
    for (const char* key : {kOfxImageEffectHostPropIsBackground, kOfxImageEffectPropSupportsOverlays,
        kOfxImageEffectPropSupportsMultiResolution, kOfxImageEffectPropSupportsTiles,
        kOfxImageEffectPropTemporalClipAccess, kOfxImageEffectPropSupportsMultipleClipDepths,
        kOfxImageEffectPropSupportsMultipleClipPARs, kOfxImageEffectPropSetableFrameRate,
        kOfxImageEffectPropSetableFielding, kOfxParamHostPropSupportsStringAnimation,
        kOfxParamHostPropSupportsCustomInteract, kOfxParamHostPropSupportsChoiceAnimation,
        kOfxParamHostPropSupportsBooleanAnimation, kOfxParamHostPropSupportsCustomAnimation,
        kOfxParamHostPropMaxParameters, kOfxParamHostPropMaxPages}) set(handle(p), key, 0, 0);
    set(handle(p), kOfxParamHostPropPageRowColumnCount, 0, 0);
    set(handle(p), kOfxParamHostPropPageRowColumnCount, 1, 0);
    set<void*>(handle(p), kOfxPropHostOSHandle, 0, nullptr);
    return p;
}
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void checkContext(OfxPlugin& plugin, const char* context) {
    Effect descriptor;
    setString(handle(descriptor.properties), kOfxImageEffectPropContext, 0, context);
    Properties args;
    setString(handle(args), kOfxImageEffectPropContext, 0, context);
    require(plugin.mainEntry(kOfxImageEffectActionDescribeInContext, &descriptor,
                            handle(args), nullptr) == kOfxStatOK, "DescribeInContext failed");
    const bool general = !std::strcmp(context, kOfxImageEffectContextGeneral);
    require(descriptor.clips.count("Source") && descriptor.clips.count("Output"), "Missing source/output");
    require(descriptor.clips.size() == (general ? 3u : 2u),
        std::string(context) + ": expected " + (general ? "3" : "2") +
        " clips, got " + std::to_string(descriptor.clips.size()));
    if (general) {
        int optional = 0;
        get(handle(descriptor.clips.at("AlphaHint")), kOfxImageClipPropOptional, 0, &optional);
        require(optional == 1, "General AlphaHint must remain optional");
    }
    Effect instance = descriptor;
    require(plugin.mainEntry(kOfxActionCreateInstance, &instance, nullptr, nullptr) == kOfxStatOK,
        std::string(context) + ": instance creation failed (absent AlphaHint must be supported)");
    require(plugin.mainEntry(kOfxActionDestroyInstance, &instance, nullptr, nullptr) == kOfxStatOK,
        "Instance destruction failed");
    std::cout << context << ": descriptor and instance PASS\n";
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Pass the built OFX binary");
        void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!library) throw std::runtime_error(dlerror());
        auto getPlugin = reinterpret_cast<OfxPlugin*(*)(int)>(dlsym(library, "OfxGetPlugin"));
        require(getPlugin != nullptr, "Missing OfxGetPlugin");
        initializeSuites();
        auto properties = hostProperties();
        OfxHost host{handle(properties), fetchSuite};
        auto& plugin = *getPlugin(0);
        plugin.setHost(&host);
        require(plugin.mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) == kOfxStatOK, "Plugin load failed");
        Effect descriptor;
        require(plugin.mainEntry(kOfxActionDescribe, &descriptor, nullptr, nullptr) == kOfxStatOK,
            "Plugin describe failed");
        checkContext(plugin, kOfxImageEffectContextFilter);
        checkContext(plugin, kOfxImageEffectContextGeneral);
        require(plugin.mainEntry(kOfxActionUnload, nullptr, nullptr, nullptr) == kOfxStatOK, "Unload failed");
        dlclose(library);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
