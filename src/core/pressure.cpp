#include "pressure.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace memcore {
namespace {
double Clamp(double value, double low = 0.0, double high = 100.0) {
    return std::clamp(value, low, high);
}
double LoadScore(double ratio, double critical_ratio) {
    return critical_ratio > 0.0 ? Clamp(ratio / critical_ratio * 100.0) : 0.0;
}
PressureBand Band(int value) {
    if (value >= 75) return PressureBand::Critical;
    if (value >= 50) return PressureBand::High;
    if (value >= 25) return PressureBand::Moderate;
    return PressureBand::Low;
}
PressureScore Score(double value) {
    PressureScore result;
    result.value = static_cast<int>(std::lround(Clamp(value)));
    result.band = Band(result.value);
    return result;
}
double Seconds(std::uint64_t newer, std::uint64_t older) {
    if (newer <= older || older == 0) return 0.0;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart > 0
        ? static_cast<double>(newer - older) / static_cast<double>(frequency.QuadPart) : 0.0;
}
}
const wchar_t* PressureBandName(PressureBand band) {
    switch (band) {
    case PressureBand::Low: return L"Low";
    case PressureBand::Moderate: return L"Moderate";
    case PressureBand::High: return L"High";
    case PressureBand::Critical: return L"Critical";
    }
    return L"Low";
}
std::vector<AppEntry> MemPressEngine::Update(const Snapshot& snapshot,
                                             std::vector<AppEntry> apps) {
    const double elapsed = Seconds(snapshot.captured_at_qpc, previous_captured_at_qpc_);
    double fault_rate = 0.0;
    if (elapsed > 0.0 && snapshot.system.page_faults >= previous_page_faults_)
        fault_rate = static_cast<double>(snapshot.system.page_faults - previous_page_faults_) / elapsed;

    double paging_score = 0.0;
    if (!page_fault_rates_.empty()) {
        double baseline = 0.0;
        for (double rate : page_fault_rates_) baseline += rate;
        baseline /= static_cast<double>(page_fault_rates_.size());
        paging_score = LoadScore(fault_rate, std::max(2.0 * baseline, 1.0));
    }

    const double commit_ratio = snapshot.system.commit_limit
        ? static_cast<double>(snapshot.system.commit_total) /
          static_cast<double>(snapshot.system.commit_limit) : 0.0;
    const double physical_ratio = snapshot.system.physical_total
        ? 1.0 - static_cast<double>(snapshot.system.physical_available) /
                    static_cast<double>(snapshot.system.physical_total) : 0.0;
    const double compression_ratio = snapshot.system.physical_total
        ? static_cast<double>(snapshot.system.memory_compression_working_set) /
          static_cast<double>(snapshot.system.physical_total) : 0.0;
    const double commit_score = LoadScore(commit_ratio, 0.90);
    const double physical_score = LoadScore(physical_ratio, 0.90);
    const double compression_score = LoadScore(compression_ratio, 0.10);
    system_pressure_ = Score(std::max(commit_score, physical_score) * 0.7 +
                             paging_score * 0.2 + compression_score * 0.1);

    std::uint64_t total_commit = 0;
    for (const AppEntry& app : apps) total_commit += app.commit;
    for (AppEntry& app : apps) {
        const auto old_it = app_history_.find(app.key);
        const bool has_old = old_it != app_history_.end();
        const double app_elapsed = has_old
            ? Seconds(snapshot.captured_at_qpc, old_it->second.captured_at_qpc) : 0.0;
        const double share = total_commit
            ? static_cast<double>(app.commit) / static_cast<double>(total_commit) : 0.0;
        const double contribution = LoadScore(share, 0.25);
        const double residency = app.commit
            ? Clamp(static_cast<double>(app.working_set) / static_cast<double>(app.commit), 0.0, 1.0)
            : 1.0;
        const double suffering = system_pressure_.value >= 50 ? (1.0 - residency) * 100.0 : 0.0;
        double faults = 0.0;
        double growth = 0.0;
        if (has_old && app_elapsed > 0.0) {
            const auto& old = old_it->second;
            const std::uint64_t delta = app.page_faults >= old.page_faults
                ? app.page_faults - old.page_faults : 0;
            faults = LoadScore(static_cast<double>(delta) / app_elapsed, 1000.0);
            if (app.commit > old.commit && old.commit) {
                const double rate = (static_cast<double>(app.commit) /
                                     static_cast<double>(old.commit) - 1.0) / app_elapsed;
                growth = LoadScore(rate, 0.25);
            }
        }
        app.pressure = Score(std::max(contribution, suffering) * 0.6 + faults * 0.2 + growth * 0.2);
        app_history_[app.key] = {snapshot.captured_at_qpc, app.working_set,
                                 app.commit, app.page_faults};
    }
    previous_captured_at_qpc_ = snapshot.captured_at_qpc;
    previous_page_faults_ = snapshot.system.page_faults;
    if (elapsed > 0.0) {
        page_fault_rates_.push_back(fault_rate);
        if (page_fault_rates_.size() > 8) page_fault_rates_.pop_front();
    }
    return apps;
}
}
