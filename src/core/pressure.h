#pragma once
#include "appmodel.h"
#include <cstdint>
#include <map>
#include <vector>
namespace memcore {
const wchar_t* PressureBandName(PressureBand band);
class MemPressEngine {
public:
    std::vector<AppEntry> Update(const Snapshot& snapshot, std::vector<AppEntry> apps);
    PressureScore SystemPressure() const { return system_pressure_; }
private:
    struct AppHistory {
        std::uint64_t captured_at_qpc = 0, hard_faults = 0;
        std::uint32_t noticeable_intervals = 0;
    };
    std::map<std::wstring, AppHistory> app_history_;
    std::uint64_t previous_captured_at_qpc_ = 0, previous_hard_faults_ = 0;
    std::uint32_t system_noticeable_intervals_ = 0;
    PressureScore system_pressure_;
};
}
