#include "grouper.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace memcore {
namespace {
std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
std::wstring BaseName(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}
std::wstring Description(const std::wstring& path, const std::wstring& fallback) {
    if (path.empty()) return fallback.empty() ? L"Unknown" : fallback;
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size) return fallback.empty() ? BaseName(path) : fallback;
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        return fallback.empty() ? BaseName(path) : fallback;
    struct Translation { WORD language, code_page; };
    Translation* translation = nullptr;
    UINT translation_size = 0;
    wchar_t block[64]{};
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                       reinterpret_cast<LPVOID*>(&translation), &translation_size) &&
        translation_size >= sizeof(Translation)) {
        swprintf_s(block, L"\\StringFileInfo\\%04x%04x\\FileDescription",
                   translation[0].language, translation[0].code_page);
    } else {
        wcscpy_s(block, L"\\StringFileInfo\\040904b0\\FileDescription");
    }
    wchar_t* value = nullptr;
    UINT value_size = 0;
    if (VerQueryValueW(data.data(), block, reinterpret_cast<LPVOID*>(&value), &value_size) &&
        value && value_size > 1 && value[0] != L'\0') return value;
    return fallback.empty() ? BaseName(path) : fallback;
}
struct WindowState { std::set<DWORD>* process_ids; };
BOOL CALLBACK CollectWindow(HWND window, LPARAM parameter) {
    auto* state = reinterpret_cast<WindowState*>(parameter);
    if (IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        if (process_id) state->process_ids->insert(process_id);
    }
    return TRUE;
}
std::wstring PathOf(const ProcessInfo& process) {
    return process.exe_path.empty() ? process.image_name : process.exe_path;
}
void Add(AppEntry& app, const ProcessInfo& process) {
    app.working_set += process.working_set;
    app.commit += process.commit;
    app.hard_faults += process.hard_faults;
    app.process_ids.push_back(process.process_id);
    if (app.exe_path.empty()) app.exe_path = PathOf(process);
    if (app.display_name.empty()) {
        const std::wstring path = PathOf(process);
        app.display_name = Description(path, BaseName(path));
    }
}
}
std::vector<AppEntry> GroupProcesses(const Snapshot& snapshot) {
    std::set<DWORD> visible;
    WindowState state{&visible};
    EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&state));

    std::map<std::wstring, std::size_t> group_indexes;
    std::vector<AppEntry> apps;
    const auto background = [&]() {
        const std::wstring key = kBackgroundAppKey;
        const auto found = group_indexes.find(key);
        if (found != group_indexes.end()) return found->second;
        AppEntry app;
        app.key = key;
        app.display_name = L"Background & OS";
        apps.push_back(std::move(app));
        group_indexes.emplace(key, apps.size() - 1);
        return apps.size() - 1;
    };

    std::map<DWORD, std::size_t> indexes;
    for (std::size_t i = 0; i < snapshot.processes.size(); ++i)
        indexes.emplace(snapshot.processes[i].process_id, i);

    // Walk to the topmost ancestor below explorer, so helper processes with
    // their own windows (e.g. steamwebhelper.exe under steam.exe) fold into
    // one app instead of splitting the tree at the first visible process.
    const auto root_for = [&](std::size_t start) {
        std::set<DWORD> visited;
        std::size_t current = start;
        bool any_visible = false;
        const auto finish = [&] {
            return any_visible ? current : snapshot.processes.size();
        };
        for (;;) {
            const ProcessInfo& process = snapshot.processes[current];
            any_visible = any_visible || visible.contains(process.process_id);
            if (!process.parent_process_id ||
                !visited.insert(process.process_id).second) return finish();
            const auto parent = indexes.find(process.parent_process_id);
            if (parent == indexes.end()) return finish();
            const ProcessInfo& parent_process = snapshot.processes[parent->second];
            if (process.create_time && parent_process.create_time &&
                parent_process.create_time > process.create_time)
                return finish();
            if (_wcsicmp(BaseName(PathOf(parent_process)).c_str(), L"explorer.exe") == 0)
                return current;
            current = parent->second;
        }
    };

    // Processes running the same exe map the same images, so their shared
    // working sets overlap almost entirely. Charging each app the largest
    // shared set per distinct exe (instead of the sum, which double-counts, or
    // nothing, which hides framework DLLs entirely) gives a ballpark of the
    // app's real footprint. System DLLs get counted once per app; accepted as
    // rounding error.
    std::map<std::pair<std::size_t, std::wstring>, std::uint64_t> shared_by_app_exe;

    for (std::size_t i = 0; i < snapshot.processes.size(); ++i) {
        const ProcessInfo& process = snapshot.processes[i];
        std::wstring key;
        std::size_t root = snapshot.processes.size();
        if (!process.package_family_name.empty()) {
            key = L"package:" + Lower(process.package_family_name);
        } else {
            root = root_for(i);
            if (root != snapshot.processes.size()) {
                const ProcessInfo& root_process = snapshot.processes[root];
                if (!root_process.package_family_name.empty()) {
                    // Unpackaged child of a packaged app (e.g. the Claude Code
                    // CLI spawned by the MSIX Claude desktop app): adopt the
                    // package's row instead of opening a desktop row for the
                    // same tree.
                    key = L"package:" + Lower(root_process.package_family_name);
                } else {
                    const std::wstring path = PathOf(root_process);
                    if (!path.empty()) key = L"desktop:" + Lower(path);
                }
            }
        }

        std::size_t app_index = apps.size();
        if (key.empty()) {
            app_index = background();
        } else {
            const auto found = group_indexes.find(key);
            if (found != group_indexes.end()) {
                app_index = found->second;
            } else {
                // Name the group after its root, not the process encountered
                // first; enumeration order is arbitrary, and a helper-first
                // walk must not label the app with the helper's identity.
                const ProcessInfo& identity =
                    root != snapshot.processes.size() ? snapshot.processes[root] : process;
                AppEntry app;
                app.key = key;
                app.package_family_name = identity.package_family_name;
                app.exe_path = PathOf(identity);
                app.display_name = Description(app.exe_path, BaseName(app.exe_path));
                apps.push_back(std::move(app));
                app_index = apps.size() - 1;
                group_indexes.emplace(key, app_index);
            }
        }
        Add(apps[app_index], process);
        if (process.working_set_shared) {
            auto& largest = shared_by_app_exe[{app_index, Lower(PathOf(process))}];
            largest = std::max(largest, process.working_set_shared);
        }
    }
    for (const auto& [app_exe, shared] : shared_by_app_exe)
        apps[app_exe.first].working_set += shared;

    // Kernel pool memory belongs to no process, so it would otherwise be
    // invisible in the app list (e.g. a driver leaking non-paged pool). Charge
    // it to the OS row. Per-process pool quotas must never be surfaced as
    // columns, or these bytes would be counted twice.
    AppEntry& os_row = apps[background()];
    const std::uint64_t pool =
        snapshot.system.kernel_paged_pool + snapshot.system.kernel_nonpaged_pool;
    os_row.working_set += pool;
    os_row.commit += pool;
    return apps;
}
}
