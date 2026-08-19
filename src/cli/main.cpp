#include "format.h"
#include "grouper.h"
#include "pressure.h"
#include "snapshot.h"
#include "version.h"
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {
volatile LONG stop_requested = 0;
BOOL WINAPI ConsoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
        type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        InterlockedExchange(&stop_requested, 1);
        return TRUE;
    }
    return FALSE;
}
std::wstring Truncate(std::wstring value, std::size_t width) {
    if (value.size() <= width) return value;
    if (width <= 3) return value.substr(0, width);
    value.resize(width - 3);
    return value + L"...";
}
std::wstring Escape(const std::wstring& value) {
    std::wstring result;
    for (wchar_t c : value) {
        switch (c) {
        case L'\\': result += L"\\\\"; break;
        case L'"': result += L"\\\""; break;
        case L'\b': result += L"\\b"; break;
        case L'\f': result += L"\\f"; break;
        case L'\n': result += L"\\n"; break;
        case L'\r': result += L"\\r"; break;
        case L'\t': result += L"\\t"; break;
        default:
            if (c < 0x20) {
                wchar_t buffer[7]{};
                swprintf_s(buffer, L"\\u%04x", static_cast<unsigned>(c));
                result += buffer;
            } else result += c;
            break;
        }
    }
    return result;
}
void Help() {
    wprintf(L"MemPressMonitor CLI %ls\n\n", MEMPRESS_VERSION_WIDE_STRINGIZE(APP_VERSION_STRING));
    wprintf(L"Usage: mempressmonitor-cli [--watch [seconds]] [--json] [--help]\n\n");
    wprintf(L"  --watch [seconds]  Refresh until Ctrl+C (default interval: 2 seconds)\n");
    wprintf(L"  --json              Emit machine-readable JSON\n");
    wprintf(L"  --help              Show this help\n");
}
void Json(const memcore::Snapshot& snapshot, const std::vector<memcore::AppEntry>& apps,
          const memcore::PressureScore& system) {
    wprintf(L"{\"system\":{\"score\":%d,\"band\":\"%ls\",\"physical_total\":%llu,"
             L"\"physical_available\":%llu,\"commit_total\":%llu,\"commit_limit\":%llu,"
             L"\"kernel_paged_pool\":%llu,\"kernel_nonpaged_pool\":%llu},"
             L"\"apps\":[", system.value, memcore::PressureBandName(system.band),
             static_cast<unsigned long long>(snapshot.system.physical_total),
             static_cast<unsigned long long>(snapshot.system.physical_available),
             static_cast<unsigned long long>(snapshot.system.commit_total),
             static_cast<unsigned long long>(snapshot.system.commit_limit),
             static_cast<unsigned long long>(snapshot.system.kernel_paged_pool),
             static_cast<unsigned long long>(snapshot.system.kernel_nonpaged_pool));
    for (std::size_t i = 0; i < apps.size(); ++i) {
        if (i) wprintf(L",");
        const auto& app = apps[i];
        const std::wstring name = Escape(app.display_name);
        wprintf(L"{\"name\":\"%ls\",\"working_set\":%llu,\"commit\":%llu,"
                 L"\"gpu_shared\":%llu,\"pressure\":%d,\"band\":\"%ls\"}", name.c_str(),
                 static_cast<unsigned long long>(app.working_set),
                 static_cast<unsigned long long>(app.commit),
                 static_cast<unsigned long long>(app.gpu_shared), app.pressure.value,
                 memcore::PressureBandName(app.pressure.band));
    }
    wprintf(L"]}\n");
}
void Table(const memcore::Snapshot& snapshot, std::vector<memcore::AppEntry> apps,
           const memcore::PressureScore& system) {
    std::sort(apps.begin(), apps.end(), [](const auto& a, const auto& b) {
        const bool a_background = a.key == memcore::kBackgroundAppKey;
        const bool b_background = b.key == memcore::kBackgroundAppKey;
        if (a_background != b_background) return b_background;
        return a.working_set != b.working_set ? a.working_set > b.working_set
                                               : a.display_name < b.display_name;
    });
    wprintf(L"System pressure: %d/100 (%ls) | RAM %ls available / %ls | Commit %ls / %ls\n",
             system.value, memcore::PressureBandName(system.band),
             mempress::FormatMegabytes(snapshot.system.physical_available).c_str(),
             mempress::FormatMegabytes(snapshot.system.physical_total).c_str(),
             mempress::FormatMegabytes(snapshot.system.commit_total).c_str(),
             mempress::FormatMegabytes(snapshot.system.commit_limit).c_str());
    wprintf(L"%-36ls %12ls %12ls %12ls %12ls\n",
             L"App", L"WorkingSet", L"Commit", L"GPU shared", L"Pressure");
    wprintf(L"%ls\n", L"--------------------------------------------------------------------------------------");
    for (const auto& app : apps) {
        const std::wstring pressure = std::to_wstring(app.pressure.value) + L" (" +
                                      memcore::PressureBandName(app.pressure.band) + L")";
        wprintf(L"%-36ls %12ls %12ls %12ls %12ls\n",
                Truncate(app.display_name, 36).c_str(), mempress::FormatMegabytes(app.working_set).c_str(),
                mempress::FormatMegabytes(app.commit).c_str(),
                mempress::FormatMegabytes(app.gpu_shared).c_str(), pressure.c_str());
    }
}
bool Capture(memcore::MemPressEngine& engine, memcore::Snapshot& snapshot,
             std::vector<memcore::AppEntry>& apps) {
    std::wstring error;
    if (!memcore::CaptureSnapshot(snapshot, error)) {
        fwprintf(stderr, L"Unable to capture process snapshot: %ls\n", error.c_str());
        return false;
    }
    apps = engine.Update(snapshot, memcore::GroupProcesses(snapshot));
    return true;
}
void Print(const memcore::Snapshot& snapshot, const std::vector<memcore::AppEntry>& apps,
           const memcore::PressureScore& system, bool json, bool clear) {
    if (clear && !json) wprintf(L"\x1b[2J\x1b[H");
    if (json) Json(snapshot, apps, system); else Table(snapshot, apps, system);
}
bool Wait(double seconds) {
    const DWORD total = static_cast<DWORD>(seconds * 1000.0);
    DWORD elapsed = 0;
    while (elapsed < total && InterlockedCompareExchange(&stop_requested, 0, 0) == 0) {
        const DWORD amount = std::min<DWORD>(100, total - elapsed);
        Sleep(amount);
        elapsed += amount;
    }
    return InterlockedCompareExchange(&stop_requested, 0, 0) == 0;
}
}
int wmain(int argc, wchar_t* argv[]) {
    bool watch = false, json = false;
    double interval = 2.0;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h") { Help(); return 0; }
        if (arg == L"--json") { json = true; continue; }
        if (arg == L"--watch") {
            watch = true;
            if (i + 1 < argc && argv[i + 1][0] != L'-') {
                wchar_t* end = nullptr;
                interval = wcstod(argv[++i], &end);
                if (end == argv[i] || *end || interval <= 0.0) {
                    fwprintf(stderr, L"--watch interval must be positive.\n");
                    return 2;
                }
            }
            continue;
        }
        fwprintf(stderr, L"Unknown argument: %ls\n", arg.c_str());
        Help();
        return 2;
    }

    _setmode(_fileno(stdout), json ? _O_U8TEXT : _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    memcore::MemPressEngine engine;
    memcore::Snapshot snapshot;
    std::vector<memcore::AppEntry> apps;
    if (!Capture(engine, snapshot, apps)) return 1;

    if (!watch) {
        if (!Wait(0.5) || !Capture(engine, snapshot, apps)) return 1;
        Print(snapshot, apps, engine.SystemPressure(), json, false);
        return 0;
    }

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    if (!json) {
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(out, &mode))
            SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    Print(snapshot, apps, engine.SystemPressure(), json, false);
    while (Wait(interval)) {
        if (!Capture(engine, snapshot, apps)) {
            SetConsoleCtrlHandler(ConsoleHandler, FALSE);
            return 1;
        }
        Print(snapshot, apps, engine.SystemPressure(), json, true);
    }
    SetConsoleCtrlHandler(ConsoleHandler, FALSE);
    return 0;
}
