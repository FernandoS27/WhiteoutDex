// MaxCore — Progress callback interface
#pragma once

#include <functional>

namespace core {
    // Progress callback: (current, total, message) -> return false to cancel
    using ProgressCallback = std::function<bool(int, int, const wchar_t*)>;
}
