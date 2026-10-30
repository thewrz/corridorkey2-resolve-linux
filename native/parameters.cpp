#include "parameters.hpp"
#include "schema.hpp"
#include <map>
#include <set>
namespace ck {
const Json& schema() { static const auto value = Json::parse(parameterSchema); return value; }
namespace {
OFX::ParamDescriptor* makeParam(OFX::ImageEffectDescriptor& desc, const Json& item) {
    const std::string name = item.at("name"), type = item.at("type");
    if (type == "button") return desc.definePushButtonParam(name);
    if (type == "bool") {
        auto* param = desc.defineBooleanParam(name);
        param->setDefault(item.at("default").get<bool>()); return param;
    }
    if (type == "choice") {
        auto* param = desc.defineChoiceParam(name);
        for (const auto& option : item.at("options")) param->appendOption(option.get<std::string>());
        param->setDefault(item.at("default").get<int>()); return param;
    }
    if (type == "int") {
        auto* param = desc.defineIntParam(name);
        param->setDefault(item.at("default").get<int>());
        param->setRange(item.at("min"), item.at("max"));
        param->setDisplayRange(item.value("display_min", item["min"]), item.value("display_max", item["max"]));
        return param;
    }
    if (type == "double") {
        auto* param = desc.defineDoubleParam(name);
        param->setDefault(item.at("default").get<double>());
        param->setRange(item.at("min"), item.at("max"));
        param->setDisplayRange(item.value("display_min", item["min"]), item.value("display_max", item["max"]));
        param->setIncrement(0.01); return param;
    }
    auto* param = desc.defineStringParam(name);
    param->setDefault(item.at("default").get<std::string>());
    param->setStringType(type == "directory" ? OFX::eStringTypeDirectoryPath : OFX::eStringTypeMultiLine);
    if (type == "label") { param->setEnabled(false); param->setEvaluateOnChange(false); }
    return param;
}
}
void defineParameters(OFX::ImageEffectDescriptor& desc) {
    auto* page = desc.definePageParam("Controls");
    std::map<std::string, OFX::GroupParamDescriptor*> groups;
    for (const auto& group : schema().at("groups")) {
        auto* param = desc.defineGroupParam(group[0]);
        param->setLabel(group[1]); param->setOpen(group[2]);
        groups[group[0]] = param; page->addChild(*param);
    }
    for (const auto& item : schema().at("params")) {
        auto* param = makeParam(desc, item);
        param->setLabel(item.at("label"));
        param->setHint(item.value("hint", ""));
        param->setParent(*groups.at(item.at("group")));
        if (auto* value = dynamic_cast<OFX::ValueParamDescriptor*>(param)) {
            value->setAnimates(false);
            value->setEvaluateOnChange(false);
        }
        page->addChild(*param);
    }
    auto* applied = desc.defineStringParam("applied_settings");
    applied->setDefault(""); applied->setIsSecret(true);
    applied->setAnimates(false); applied->setEvaluateOnChange(false);
    auto* revision = desc.defineIntParam("applied_revision");
    revision->setDefault(0); revision->setIsSecret(true); revision->setAnimates(false);
    revision->setCacheInvalidation(OFX::eCacheInvalidateValueAll);
    desc.addClipPreferencesSlaveParam(*revision);
}
Json readSettings(OFX::ImageEffect& effect, double time) {
    Json result = Json::object();
    const std::set<std::string> hostOnly{"status", "resolution", "cache_folder", "temporal",
        "auto_range", "shot_start", "shot_end", "help"};
    for (const auto& item : schema().at("params")) {
        const std::string name = item.at("name"), type = item.at("type");
        if (hostOnly.count(name) || type == "button") continue;
        if (type == "bool") result[name] = effect.fetchBooleanParam(name)->getValueAtTime(time);
        if (type == "int") result[name] = effect.fetchIntParam(name)->getValueAtTime(time);
        if (type == "double") result[name] = effect.fetchDoubleParam(name)->getValueAtTime(time);
        if (type == "choice") {
            int value; effect.fetchChoiceParam(name)->getValueAtTime(time, value); result[name] = value;
        }
    }
    int resolution; effect.fetchChoiceParam("resolution")->getValueAtTime(time, resolution);
    result["model_scale"] = resolution == 0 ? 1.0 : resolution == 1 ? 0.5 : 0.25;
    return result;
}
Json readSnapshot(OFX::ImageEffect& effect, double time) {
    std::string folder; effect.fetchStringParam("cache_folder")->getValue(folder);
    return {{"settings", readSettings(effect, time)}, {"cache_folder", folder},
        {"temporal", effect.fetchBooleanParam("temporal")->getValueAtTime(time)},
        {"auto_range", effect.fetchBooleanParam("auto_range")->getValueAtTime(time)},
        {"shot_start", effect.fetchIntParam("shot_start")->getValueAtTime(time)},
        {"shot_end", effect.fetchIntParam("shot_end")->getValueAtTime(time)}};
}
}
