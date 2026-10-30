#pragma once
#include "bridge.hpp"
#include "ofxsImageEffect.h"
namespace ck {
const Json& schema();
void defineParameters(OFX::ImageEffectDescriptor& desc);
Json readSettings(OFX::ImageEffect& effect, double time);
Json readSnapshot(OFX::ImageEffect& effect, double time);
}
