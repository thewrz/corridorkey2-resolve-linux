#include "host.hpp"
#include "ofxsImageEffect.h"
#include <iostream>

void OFX::Plugin::getPluginIDs(OFX::PluginFactoryArray&) {}

int main() {
    try {
        if (!ck::notifyHostIfSupported([] {})) return 1;
        if (ck::notifyHostIfSupported([] { throw OFX::Exception::Suite(kOfxStatErrUnsupported); }))
            return 2;
        bool propagated = false;
        try { ck::notifyHostIfSupported([] { throw OFX::Exception::Suite(kOfxStatErrMemory); }); }
        catch (const OFX::Exception::Suite& error) { propagated = error.status() == kOfxStatErrMemory; }
        if (!propagated) return 3;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 4;
    }
}
