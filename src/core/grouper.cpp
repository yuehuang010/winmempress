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
        app.display_name = L"Background & system";
        apps.push_back(std::move(app));
        group_indexes.emplace(key, apps.size() - 1);
        return apps.size() - 1;
    };

    std::map<DWORD, std::size_t> indexes;
    for (std::size_t i = 0; i < snapshot.processes.size(); ++i)
        indexes.emplace(snapshot.processes[i].process_id, i);

    const auto root_for = [&](std::size_t start) {
        std::set<DWORD> visited;
        std::size_t current = start;
        for (;;) {
            const ProcessInfo& process = snapshot.processes[current];
            if (visible.contains(process.process_id)) return current;
            if (!process.parent_process_id ||
                !visited.insert(process.process_id).second) return snapshot.processes.size();
            const auto parent = indexes.find(process.parent_process_id);
            if (parent == indexes.end()) return snapshot.processes.size();
            const ProcessInfo& parent_process = snapshot.processes[parent->second];
            if (process.create_time && parent_process.create_time &&
                parent_process.create_time > process.create_time)
                return snapshot.processes.size();
            if (_wcsicmp(BaseName(PathOf(parent_process)).c_str(), L"explorer.exe") == 0)
                return current;
            current = parent->second;
        }
    };

    for (std::size_t i = 0; i < snapshot.processes.size(); ++i) {
        const ProcessInfo& process = snapshot.processes[i];
        std::wstring key;
        if (!process.package_family_name.empty()) {
            key = L"package:" + Lower(process.package_family_name);
        } else {
            const std::size_t root = root_for(i);
            if (root != snapshot.processes.size()) {
                const std::wstring path = PathOf(snapshot.processes[root]);
                if (!path.empty()) key = L"desktop:" + Lower(path);
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
                AppEntry app;
                app.key = key;
                app.package_family_name = process.package_family_name;
                app.exe_path = PathOf(process);
                app.display_name = Description(app.exe_path, BaseName(app.exe_path));
                apps.push_back(std::move(app));
                app_index = apps.size() - 1;
                group_indexes.emplace(key, app_index);
            }
        }
        Add(apps[app_index], process);
    }
    return apps;
}
}
