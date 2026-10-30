#pragma once
#include "ofxsCore.h"

namespace ck {
template <typename Notify> bool notifyHostIfSupported(Notify notify) {
    try {
        notify();
        return true;
    } catch (const OFX::Exception::Suite& error) {
        // Fusion lacks the optional persistent-message suite. Inspector status
        // remains available, and this must not turn a good image into a failure.
        if (error.status() == kOfxStatErrUnsupported) return false;
        throw;
    }
}
}
