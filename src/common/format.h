#pragma once

#include <cstdint>
#include <string>

namespace mempress {

// Memory sizes are always rendered in whole MB (no KB/GB units), per product
// decision: one unit everywhere so values stay directly comparable.
inline std::wstring FormatMegabytes(std::uint64_t bytes) {
    std::wstring digits = std::to_wstring(bytes / (1024ULL * 1024ULL));
    for (std::size_t i = digits.size(); i > 3;) {
        i -= 3;
        digits.insert(i, L",");
    }
    return digits + L" MB";
}

}
