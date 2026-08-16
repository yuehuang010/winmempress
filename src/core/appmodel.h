#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace memcore {
inline constexpr wchar_t kBackgroundAppKey[] = L"background";
enum class PressureBand { Low, Moderate, High, Critical };
struct PressureScore { int value = 0; PressureBand band = PressureBand::Low; };
struct ProcessInfo {
    DWORD process_id = 0, parent_process_id = 0, session_id = 0;
    std::uint64_t create_time = 0, working_set = 0, commit = 0, hard_faults = 0;
    std::wstring image_name, exe_path, package_family_name;
};
struct SystemStats {
    std::uint64_t commit_total = 0, commit_limit = 0, physical_total = 0;
    std::uint64_t physical_available = 0, hard_faults = 0;
};
struct Snapshot {
    std::vector<ProcessInfo> processes;
    SystemStats system;
    std::uint64_t captured_at_qpc = 0;
};
struct AppEntry {
    std::wstring key, display_name, exe_path, package_family_name;
    std::uint64_t working_set = 0, commit = 0, hard_faults = 0;
    std::vector<DWORD> process_ids;
    PressureScore pressure;
};
}
