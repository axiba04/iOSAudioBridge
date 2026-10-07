#pragma once
#include "AudioCore.h"
#include <functional>

namespace audiobridge {
// Called by the capture worker, never by the WASAPI render thread.
struct SourceLifecycle {
    std::function<void(const AB_Config&, uint32_t)> prepare_output;
    std::function<void()> check_output;
    std::function<void()> stop_output;
};
}
