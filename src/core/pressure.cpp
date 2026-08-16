#include "pressure.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace memcore {
namespace {
constexpr double kMeasurableFaultsPerSec = 10.0;
constexpr double kNoticeableFaultsPerSec = 100.0;
constexpr std::uint32_t kSustainedIntervals = 3;
constexpr double kSystemNoticeableFaultsPerSec = 500.0;
constexpr double kCriticalMemoryLoad = 0.90;
constexpr double kLatentMemoryLoad = 0.80;

double Clamp(double value, double low = 0.0, double high = 100.0) {
    return std::clamp(value, low, high);
}

double LoadScore(double ratio, double critical_ratio) {
    return critical_ratio > 0.0 ? Clamp(ratio / critical_ratio * 100.0) : 0.0;
}

PressureBand Band(double value) {
    if (value >= 75.0) return PressureBand::Critical;
    if (value >= 50.0) return PressureBand::High;
    if (value >= 25.0) return PressureBand::Moderate;
    return PressureBand::Low;
}

double LinearScore(double value, double input_low, double input_high,
                   double score_low, double score_high) {
    if (input_high <= input_low) return score_low;
    const double fraction = Clamp((value - input_low) / (input_high - input_low), 0.0, 1.0);
    return score_low + fraction * (score_high - score_low);
}

PressureScore ScoreInBand(double value, PressureBand band) {
    const double low = band == PressureBand::Low ? 0.0
        : band == PressureBand::Moderate ? 25.0
        : band == PressureBand::High ? 50.0 : 75.0;
    const double high = band == PressureBand::Low ? 24.0
        : band == PressureBand::Moderate ? 49.0
        : band == PressureBand::High ? 74.0 : 100.0;
    PressureScore result;
    result.value = static_cast<int>(std::lround(Clamp(value, low, high)));
    result.band = band;
    return result;
}

double Seconds(std::uint64_t newer, std::uint64_t older) {
    if (newer <= older || older == 0) return 0.0;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart > 0
        ? static_cast<double>(newer - older) / static_cast<double>(frequency.QuadPart) : 0.0;
}

double FaultRate(std::uint64_t current, std::uint64_t previous, double elapsed) {
    if (elapsed <= 0.0 || current < previous) return 0.0;
    return static_cast<double>(current - previous) / elapsed;
}

PressureScore AppScore(double rate, double working_set, double commit, double memory_load,
                       std::uint32_t noticeable_intervals) {
    const bool latent_trim = commit > 0 && working_set / commit < 0.5 &&
                             memory_load >= kLatentMemoryLoad;
    PressureBand band = PressureBand::Low;
    double score = 0.0;
    if (rate >= kNoticeableFaultsPerSec) {
        if (noticeable_intervals >= kSustainedIntervals) {
            band = PressureBand::Critical;
            score = LinearScore(rate, kNoticeableFaultsPerSec,
                                10.0 * kNoticeableFaultsPerSec, 75.0, 100.0);
        } else {
            band = PressureBand::High;
            score = LinearScore(rate, kNoticeableFaultsPerSec,
                                10.0 * kNoticeableFaultsPerSec, 50.0, 74.0);
        }
    } else if (rate >= kMeasurableFaultsPerSec) {
        band = PressureBand::Moderate;
        score = LinearScore(rate, kMeasurableFaultsPerSec,
                            kNoticeableFaultsPerSec, 25.0, 49.0);
    } else if (latent_trim) {
        band = PressureBand::Moderate;
        score = 25.0 + (1.0 - Clamp(working_set / commit, 0.0, 1.0)) * 24.0;
    } else {
        score = LinearScore(rate, 0.0, kMeasurableFaultsPerSec, 0.0, 24.0);
    }
    return ScoreInBand(score, band);
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
    const double system_fault_rate = FaultRate(snapshot.system.hard_faults,
                                               previous_hard_faults_, elapsed);
    if (system_fault_rate >= kSystemNoticeableFaultsPerSec) {
        ++system_noticeable_intervals_;
    } else {
        system_noticeable_intervals_ = 0;
    }

    const double commit_ratio = snapshot.system.commit_limit
        ? static_cast<double>(snapshot.system.commit_total) /
          static_cast<double>(snapshot.system.commit_limit) : 0.0;
    const double physical_ratio = snapshot.system.physical_total
        ? 1.0 - static_cast<double>(snapshot.system.physical_available) /
                    static_cast<double>(snapshot.system.physical_total) : 0.0;
    const double memory_load = std::max(commit_ratio, physical_ratio);
    const double load_score = LoadScore(memory_load, kCriticalMemoryLoad);
    const double fault_score = LoadScore(system_fault_rate,
                                         10.0 * kSystemNoticeableFaultsPerSec);
    const double raw_score = 0.5 * load_score + 0.5 * fault_score;

    PressureBand system_band = Band(raw_score);
    if (system_fault_rate >= kSystemNoticeableFaultsPerSec &&
        (system_band == PressureBand::Low || system_band == PressureBand::Moderate)) {
        system_band = PressureBand::High;
    }
    if (system_band == PressureBand::Critical &&
        system_noticeable_intervals_ < kSustainedIntervals) {
        system_band = PressureBand::High;
    }
    system_pressure_ = ScoreInBand(raw_score, system_band);

    for (AppEntry& app : apps) {
        const auto old_it = app_history_.find(app.key);
        const bool has_old = old_it != app_history_.end();
        const double app_elapsed = has_old
            ? Seconds(snapshot.captured_at_qpc, old_it->second.captured_at_qpc) : 0.0;
        const double app_fault_rate = has_old
            ? FaultRate(app.hard_faults, old_it->second.hard_faults, app_elapsed) : 0.0;
        std::uint32_t noticeable_intervals = has_old
            ? old_it->second.noticeable_intervals : 0;
        if (app_fault_rate >= kNoticeableFaultsPerSec) {
            ++noticeable_intervals;
        } else {
            noticeable_intervals = 0;
        }
        app.pressure = AppScore(app_fault_rate,
                                static_cast<double>(app.working_set),
                                static_cast<double>(app.commit), memory_load,
                                noticeable_intervals);
        app_history_[app.key] = {snapshot.captured_at_qpc, app.hard_faults,
                                 noticeable_intervals};
    }

    previous_captured_at_qpc_ = snapshot.captured_at_qpc;
    previous_hard_faults_ = snapshot.system.hard_faults;
    return apps;
}
}
