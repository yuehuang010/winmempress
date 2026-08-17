#include "resource.h"
#include "grouper.h"
#include "pressure.h"
#include "snapshot.h"

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kRefreshMessage = WM_APP + 1;

struct WorkerResult {
    bool success = false;
    std::wstring error;
    std::vector<memcore::AppEntry> apps;
    memcore::PressureScore system_pressure;
};

struct RenderedRow {
    std::wstring name;
    std::wstring memory;
    std::wstring pressure;
    int image = -1;
};

struct DialogState {
    HWND dialog = nullptr;
    HWND list = nullptr;
    HANDLE stop_event = nullptr;
    HANDLE refresh_event = nullptr;
    std::thread worker;
    std::vector<memcore::AppEntry> all_apps;
    std::vector<memcore::AppEntry> apps;
    std::vector<RenderedRow> rendered_rows;
    bool dark = false;
    HBRUSH background_brush = nullptr;
    bool owns_background_brush = false;
    int sort_column = 1;
    bool sort_ascending = false;
    bool show_all = false;
    HICON icon_big = nullptr;
    HICON icon_small = nullptr;
    HIMAGELIST image_list = nullptr;
    int small_icon_height = 0;
    int fallback_image = -1;
    std::map<std::wstring, int> icon_cache;
};

std::wstring FormatMemory(std::uint64_t bytes) {
    std::wostringstream output;
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        output << std::fixed << std::setprecision(1)
               << static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0) << L" GB";
    } else {
        output << std::fixed << std::setprecision(0)
               << static_cast<double>(bytes) / (1024.0 * 1024.0) << L" MB";
    }
    return output.str();
}

bool IsDarkTheme() {
    HKEY key = nullptr;
    constexpr wchar_t path[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LONG result = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && value == 0;
}

void ApplyTitleBarTheme(HWND dialog, bool dark) {
    const BOOL enabled = dark ? TRUE : FALSE;
    constexpr DWORD immersive_dark_mode = 20;
    if (DwmSetWindowAttribute(dialog, immersive_dark_mode, &enabled, sizeof(enabled)) != S_OK) {
        constexpr DWORD legacy_immersive_dark_mode = 19;
        DwmSetWindowAttribute(dialog, legacy_immersive_dark_mode, &enabled, sizeof(enabled));
    }
}

void ApplyTheme(HWND dialog, DialogState& state) {
    state.dark = IsDarkTheme();
    if (state.owns_background_brush && state.background_brush)
        DeleteObject(state.background_brush);
    state.background_brush = state.dark
        ? CreateSolidBrush(RGB(32, 32, 32))
        : GetSysColorBrush(COLOR_WINDOW);
    state.owns_background_brush = state.dark;

    ApplyTitleBarTheme(dialog, state.dark);
    for (const int control : {IDC_END_TASK, IDC_PIN_TOPMOST, IDC_EXPAND}) {
        const HWND button = GetDlgItem(dialog, control);
        if (button)
            SetWindowTheme(button, state.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
    if (state.list) {
        const COLORREF background = state.dark ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW);
        const COLORREF text = state.dark ? RGB(245, 245, 245) : GetSysColor(COLOR_WINDOWTEXT);
        ListView_SetBkColor(state.list, background);
        ListView_SetTextBkColor(state.list, background);
        ListView_SetTextColor(state.list, text);
        SetWindowTheme(state.list, state.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        const HWND header = ListView_GetHeader(state.list);
        if (header)
            SetWindowTheme(header, state.dark ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);
        InvalidateRect(state.list, nullptr, TRUE);
    }
    InvalidateRect(dialog, nullptr, TRUE);
}

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

void LayoutControls(HWND dialog, DialogState& state) {
    RECT client{};
    GetClientRect(dialog, &client);
    const UINT dpi = GetDpiForWindow(dialog);
    const int margin = ScaleForDpi(8, dpi);
    const int gap = ScaleForDpi(8, dpi);
    const int button_width = ScaleForDpi(90, dpi);
    const int button_height = ScaleForDpi(26, dpi);
    RECT button{client.right - margin - button_width, client.bottom - margin - button_height,
                client.right - margin, client.bottom - margin};
    MoveWindow(GetDlgItem(dialog, IDC_END_TASK), button.left, button.top,
               button_width, button_height, TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_PIN_TOPMOST), margin, button.top,
               ScaleForDpi(130, dpi), button_height, TRUE);
    MoveWindow(GetDlgItem(dialog, IDC_EXPAND), margin + ScaleForDpi(130, dpi) + gap,
               button.top, button_width, button_height, TRUE);
    MoveWindow(state.list, margin, margin, client.right - margin * 2,
               button.top - gap - margin, TRUE);

    RECT list_client{};
    GetClientRect(state.list, &list_client);
    const int fixed_columns = ListView_GetColumnWidth(state.list, 1) +
                              ListView_GetColumnWidth(state.list, 2);
    const int name_width = std::max(ScaleForDpi(120, dpi),
                                    static_cast<int>(list_client.right) - fixed_columns);
    ListView_SetColumnWidth(state.list, 0, name_width);
}

void UpdateEndTaskState(HWND dialog, const DialogState& state) {
    const int selected = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    const bool enabled = selected >= 0 && static_cast<std::size_t>(selected) < state.apps.size() &&
                         state.apps[static_cast<std::size_t>(selected)].key !=
                             memcore::kBackgroundAppKey;
    EnableWindow(GetDlgItem(dialog, IDC_END_TASK), enabled ? TRUE : FALSE);
}

void UpdateExpandButton(HWND dialog, const DialogState& state) {
    SetWindowTextW(GetDlgItem(dialog, IDC_EXPAND), state.show_all ? L"Show top 6" : L"Show all");
}

void AddColumns(HWND list) {
    const UINT dpi = GetDpiForWindow(list);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    column.pszText = const_cast<LPWSTR>(L"App");
    column.cx = ScaleForDpi(360, dpi);
    column.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(list, 0, &column);

    column.pszText = const_cast<LPWSTR>(L"Memory");
    column.cx = ScaleForDpi(110, dpi);
    column.fmt = LVCFMT_CENTER;
    ListView_InsertColumn(list, 1, &column);

    column.pszText = const_cast<LPWSTR>(L"Pressure");
    column.cx = ScaleForDpi(110, dpi);
    column.fmt = LVCFMT_CENTER;
    ListView_InsertColumn(list, 2, &column);
}

std::wstring LowercasePath(const std::wstring& path) {
    std::wstring result = path;
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return result;
}

void DestroySmallImageList(DialogState& state) {
    if (!state.image_list)
        return;
    const HIMAGELIST image_list =
        ListView_SetImageList(state.list, nullptr, LVSIL_SMALL);
    if (image_list)
        ImageList_Destroy(image_list);
    else
        ImageList_Destroy(state.image_list);
    state.image_list = nullptr;
}

void CreateSmallImageList(DialogState& state, UINT dpi) {
    DestroySmallImageList(state);
    state.icon_cache.clear();
    state.fallback_image = -1;

    const int cx = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    const int cy = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
    state.small_icon_height = cy;
    state.image_list = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 16, 16);
    if (!state.image_list)
        return;

    ListView_SetImageList(state.list, state.image_list, LVSIL_SMALL);

    SHSTOCKICONINFO stock_icon{};
    stock_icon.cbSize = sizeof(stock_icon);
    if (SUCCEEDED(SHGetStockIconInfo(SIID_APPLICATION,
                                     SHGSI_ICON | SHGSI_SMALLICON, &stock_icon)) &&
        stock_icon.hIcon) {
        state.fallback_image = ImageList_AddIcon(state.image_list, stock_icon.hIcon);
        DestroyIcon(stock_icon.hIcon);
    }
}

int ImageIndexForApp(DialogState& state, const std::wstring& exe_path) {
    const std::wstring cache_key = LowercasePath(exe_path);
    const auto cached = state.icon_cache.find(cache_key);
    if (cached != state.icon_cache.end())
        return cached->second;

    int image = state.fallback_image;
    if (!exe_path.empty() && state.image_list) {
        HICON extracted_icon = nullptr;
        const HRESULT result = SHDefExtractIconW(exe_path.c_str(), 0, 0, nullptr, &extracted_icon,
                                                 MAKELONG(0, state.small_icon_height));
        if (SUCCEEDED(result) && extracted_icon) {
            const int extracted = ImageList_AddIcon(state.image_list, extracted_icon);
            DestroyIcon(extracted_icon);
            if (extracted >= 0)
                image = extracted;
        } else if (extracted_icon) {
            DestroyIcon(extracted_icon);
        }
    }

    state.icon_cache.emplace(cache_key, image);
    return image;
}

int CompareApps(const memcore::AppEntry& left, const memcore::AppEntry& right, int column) {
    switch (column) {
    case 1:
        return left.working_set < right.working_set ? -1
             : left.working_set > right.working_set ? 1 : 0;
    case 2:
        return left.pressure.value - right.pressure.value;
    default:
        return _wcsicmp(left.display_name.c_str(), right.display_name.c_str());
    }
}

void UpdateSortArrows(const DialogState& state) {
    const HWND header = ListView_GetHeader(state.list);
    if (!header) return;
    for (int column = 0; column < 3; ++column) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        if (!Header_GetItem(header, column, &item)) continue;
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (column == state.sort_column)
            item.fmt |= state.sort_ascending ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(header, column, &item);
    }
}

std::vector<memcore::AppEntry> VisibleApps(const std::vector<memcore::AppEntry>& all_apps,
                                           bool show_all) {
    if (show_all) return all_apps;

    std::vector<memcore::AppEntry> visible;
    visible.reserve(std::min<std::size_t>(all_apps.size(), 7));
    std::size_t app_count = 0;
    for (const auto& app : all_apps) {
        if (app.key == memcore::kBackgroundAppKey) continue;
        if (app_count == 6) break;
        visible.push_back(app);
        ++app_count;
    }
    const auto background = std::find_if(all_apps.begin(), all_apps.end(), [](const auto& app) {
        return app.key == memcore::kBackgroundAppKey;
    });
    if (background != all_apps.end()) visible.push_back(*background);
    return visible;
}

void UpdateList(HWND dialog, DialogState& state, std::vector<memcore::AppEntry> apps) {
    std::wstring selected_key;
    const int old_selected = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    if (old_selected >= 0 && static_cast<std::size_t>(old_selected) < state.apps.size())
        selected_key = state.apps[static_cast<std::size_t>(old_selected)].key;

    if (old_selected >= 0)
        ListView_SetItemState(state.list, old_selected, 0, LVIS_SELECTED | LVIS_FOCUSED);

    state.apps = std::move(apps);
    const int old_count = ListView_GetItemCount(state.list);
    const int new_count = static_cast<int>(state.apps.size());
    std::vector<RenderedRow> rendered_rows;
    rendered_rows.reserve(state.apps.size());

    for (int index = 0; index < std::min(old_count, new_count); ++index) {
        const auto& app = state.apps[static_cast<std::size_t>(index)];
        const std::wstring memory = FormatMemory(app.working_set);
        const std::wstring pressure = std::to_wstring(app.pressure.value) + L"%";
        const int image = ImageIndexForApp(state, app.exe_path);
        const RenderedRow row{app.display_name, memory, pressure, image};

        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = index;
        item.lParam = static_cast<LPARAM>(index);
        ListView_SetItem(state.list, &item);
        if (static_cast<std::size_t>(index) >= state.rendered_rows.size() ||
            state.rendered_rows[static_cast<std::size_t>(index)].image != row.image) {
            LVITEMW image_item{};
            image_item.mask = LVIF_IMAGE;
            image_item.iItem = index;
            image_item.iImage = row.image;
            ListView_SetItem(state.list, &image_item);
        }
        if (static_cast<std::size_t>(index) >= state.rendered_rows.size() ||
            state.rendered_rows[static_cast<std::size_t>(index)].name != row.name)
            ListView_SetItemText(state.list, index, 0, const_cast<LPWSTR>(row.name.c_str()));
        if (static_cast<std::size_t>(index) >= state.rendered_rows.size() ||
            state.rendered_rows[static_cast<std::size_t>(index)].memory != row.memory)
            ListView_SetItemText(state.list, index, 1, const_cast<LPWSTR>(row.memory.c_str()));
        if (static_cast<std::size_t>(index) >= state.rendered_rows.size() ||
            state.rendered_rows[static_cast<std::size_t>(index)].pressure != row.pressure)
            ListView_SetItemText(state.list, index, 2, const_cast<LPWSTR>(row.pressure.c_str()));
        rendered_rows.push_back(row);
    }

    for (int index = old_count; index < new_count; ++index) {
        const auto& app = state.apps[static_cast<std::size_t>(index)];
        const int image = ImageIndexForApp(state, app.exe_path);
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        item.iItem = index;
        item.lParam = static_cast<LPARAM>(index);
        item.iImage = image;
        item.pszText = const_cast<LPWSTR>(app.display_name.c_str());
        ListView_InsertItem(state.list, &item);

        const std::wstring memory = FormatMemory(app.working_set);
        ListView_SetItemText(state.list, index, 1, const_cast<LPWSTR>(memory.c_str()));
        const std::wstring pressure = std::to_wstring(app.pressure.value) + L"%";
        ListView_SetItemText(state.list, index, 2, const_cast<LPWSTR>(pressure.c_str()));
        rendered_rows.push_back({app.display_name, memory, pressure, image});
    }

    for (int index = old_count - 1; index >= new_count; --index)
        ListView_DeleteItem(state.list, index);

    state.rendered_rows = std::move(rendered_rows);
    const auto selected = std::find_if(state.apps.begin(), state.apps.end(),
                                       [&selected_key](const auto& app) {
                                           return !selected_key.empty() && app.key == selected_key;
                                       });
    if (selected != state.apps.end()) {
        const int index = static_cast<int>(std::distance(state.apps.begin(), selected));
        ListView_SetItemState(state.list, index, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    UpdateEndTaskState(dialog, state);
}

void RefreshVisibleList(HWND dialog, DialogState& state) {
    UpdateList(dialog, state, VisibleApps(state.all_apps, state.show_all));
}

void RebuildList(HWND dialog, DialogState& state, std::vector<memcore::AppEntry> apps) {
    const int column = state.sort_column;
    const bool ascending = state.sort_ascending;
    std::sort(apps.begin(), apps.end(), [column, ascending](const auto& left, const auto& right) {
        const bool left_background = left.key == memcore::kBackgroundAppKey;
        const bool right_background = right.key == memcore::kBackgroundAppKey;
        if (left_background != right_background) return right_background;
        const int order = CompareApps(left, right, column);
        if (order != 0) return ascending ? order < 0 : order > 0;
        return _wcsicmp(left.display_name.c_str(), right.display_name.c_str()) < 0;
    });
    state.all_apps = std::move(apps);
    RefreshVisibleList(dialog, state);
}

void CaptureOnWorker(DialogState* state) {
    memcore::MemPressEngine engine;
    for (;;) {
        const HANDLE events[] = {state->stop_event, state->refresh_event};
        const DWORD result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0)
            return;
        if (result != WAIT_OBJECT_0 + 1)
            return;
        ResetEvent(state->refresh_event);

        auto* update = new WorkerResult;
        memcore::Snapshot snapshot;
        if (!memcore::CaptureSnapshot(snapshot, update->error)) {
            update->success = false;
        } else {
            update->apps = engine.Update(snapshot, memcore::GroupProcesses(snapshot));
            update->system_pressure = engine.SystemPressure();
            update->success = true;
        }

        if (WaitForSingleObject(state->stop_event, 0) == WAIT_OBJECT_0 ||
            !PostMessageW(state->dialog, kRefreshMessage, 0,
                          reinterpret_cast<LPARAM>(update))) {
            delete update;
            if (WaitForSingleObject(state->stop_event, 0) == WAIT_OBJECT_0)
                return;
        }
    }
}

bool StartWorker(DialogState& state) {
    state.stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state.refresh_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state.stop_event || !state.refresh_event) {
        if (state.stop_event) CloseHandle(state.stop_event);
        if (state.refresh_event) CloseHandle(state.refresh_event);
        state.stop_event = nullptr;
        state.refresh_event = nullptr;
        return false;
    }
    try {
        state.worker = std::thread(CaptureOnWorker, &state);
    } catch (...) {
        CloseHandle(state.stop_event);
        CloseHandle(state.refresh_event);
        state.stop_event = nullptr;
        state.refresh_event = nullptr;
        return false;
    }
    SetEvent(state.refresh_event);
    return true;
}

void StopWorker(HWND dialog, DialogState& state) {
    KillTimer(dialog, kRefreshTimer);
    if (state.stop_event)
        SetEvent(state.stop_event);
    if (state.worker.joinable())
        state.worker.join();
    if (state.stop_event) CloseHandle(state.stop_event);
    if (state.refresh_event) CloseHandle(state.refresh_event);
    state.stop_event = nullptr;
    state.refresh_event = nullptr;

    MSG message{};
    while (PeekMessageW(&message, dialog, kRefreshMessage, kRefreshMessage, PM_REMOVE))
        delete reinterpret_cast<WorkerResult*>(message.lParam);
}

void RequestRefresh(const DialogState& state) {
    if (state.refresh_event)
        SetEvent(state.refresh_event);
}

void EndSelectedTask(HWND dialog, DialogState& state) {
    const int selected = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= state.apps.size())
        return;
    const auto& app = state.apps[static_cast<std::size_t>(selected)];
    if (app.key == memcore::kBackgroundAppKey)
        return;

    const std::wstring message = L"End " + app.display_name + L"? This will terminate " +
                                 std::to_wstring(app.process_ids.size()) + L" process(es).";
    if (MessageBoxW(dialog, message.c_str(), L"MemPressMonitor",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    for (const DWORD process_id : app.process_ids) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
        if (!process)
            continue;
        TerminateProcess(process, 1);
        CloseHandle(process);
    }
    RequestRefresh(state);
}

LRESULT CALLBACK ListSubclassProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param,
                                  UINT_PTR, DWORD_PTR ref_data) {
    if (message == WM_NOTIFY) {
        const auto* state = reinterpret_cast<const DialogState*>(ref_data);
        const auto* notification = reinterpret_cast<const NMHDR*>(l_param);
        if (state && state->dark && notification->code == NM_CUSTOMDRAW &&
            notification->hwndFrom == ListView_GetHeader(window)) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(l_param);
            switch (draw->dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
                SetTextColor(draw->hdc, RGB(245, 245, 245));
                return CDRF_DODEFAULT;
            default:
                break;
            }
        }
    }
    return DefSubclassProc(window, message, w_param, l_param);
}

COLORREF PressureColor(memcore::PressureBand band, bool dark) {
    switch (band) {
    case memcore::PressureBand::Low:
    case memcore::PressureBand::Moderate:
        return dark ? RGB(110, 220, 125) : RGB(0, 120, 0);
    case memcore::PressureBand::High: return dark ? RGB(255, 165, 70) : RGB(200, 100, 0);
    case memcore::PressureBand::Critical: return dark ? RGB(255, 115, 115) : RGB(175, 0, 0);
    }
    return dark ? RGB(245, 245, 245) : RGB(0, 0, 0);
}

void PaintOverColumnSeparators(const NMLVCUSTOMDRAW& draw) {
    const HWND list = draw.nmcd.hdr.hwndFrom;
    const HWND header = ListView_GetHeader(list);
    RECT client{};
    GetClientRect(list, &client);
    RECT header_rect{};
    GetWindowRect(header, &header_rect);
    const int top = header_rect.bottom - header_rect.top;

    const HBRUSH brush = CreateSolidBrush(ListView_GetBkColor(list));
    for (int column = 0; column < 3; ++column) {
        RECT item{};
        if (!Header_GetItemRect(header, column, &item)) continue;
        RECT line{item.right - 2, top, item.right + 2, client.bottom};
        FillRect(draw.nmcd.hdc, &line, brush);
    }
    DeleteObject(brush);
}

LRESULT HandleCustomDraw(const DialogState& state, LPARAM parameter) {
    auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(parameter);
    switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    case CDDS_POSTPAINT:
        PaintOverColumnSeparators(*draw);
        return CDRF_DODEFAULT;
    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        const std::size_t item = static_cast<std::size_t>(draw->nmcd.dwItemSpec);
        if (draw->iSubItem == 2 && item < state.apps.size())
            draw->clrText = PressureColor(state.apps[item].pressure.band, state.dark);
        return CDRF_DODEFAULT;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    switch (message) {
    case WM_INITDIALOG: {
        auto* new_state = new DialogState;
        new_state->dialog = dialog;
        new_state->list = GetDlgItem(dialog, IDC_APP_LIST);
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        const UINT dpi = GetDpiForWindow(dialog);
        new_state->icon_big = static_cast<HICON>(
            LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                       GetSystemMetricsForDpi(SM_CXICON, dpi),
                       GetSystemMetricsForDpi(SM_CYICON, dpi), LR_DEFAULTCOLOR));
        new_state->icon_small = static_cast<HICON>(
            LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                       GetSystemMetricsForDpi(SM_CXSMICON, dpi),
                       GetSystemMetricsForDpi(SM_CYSMICON, dpi), LR_DEFAULTCOLOR));
        if (new_state->icon_big)
            SendMessageW(dialog, WM_SETICON, ICON_BIG,
                         reinterpret_cast<LPARAM>(new_state->icon_big));
        if (new_state->icon_small)
            SendMessageW(dialog, WM_SETICON, ICON_SMALL,
                         reinterpret_cast<LPARAM>(new_state->icon_small));
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(new_state));
        CreateSmallImageList(*new_state, dpi);
        ListView_SetExtendedListViewStyle(new_state->list,
                                          LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        AddColumns(new_state->list);
        SetWindowSubclass(new_state->list, ListSubclassProc, 1,
                          reinterpret_cast<DWORD_PTR>(new_state));
        UpdateSortArrows(*new_state);
        ApplyTheme(dialog, *new_state);
        LayoutControls(dialog, *new_state);
        UpdateExpandButton(dialog, *new_state);
        EnableWindow(GetDlgItem(dialog, IDC_END_TASK), FALSE);
        if (!StartWorker(*new_state)) {
            MessageBoxW(dialog, L"Unable to start the memory capture worker.", L"MemPressMonitor",
                        MB_OK | MB_ICONERROR);
            EndDialog(dialog, 1);
            return TRUE;
        }
        SetTimer(dialog, kRefreshTimer, 2000, nullptr);
        return TRUE;
    }
    case WM_SIZE:
        if (state) LayoutControls(dialog, *state);
        return TRUE;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(l_param);
        const UINT dpi = GetDpiForWindow(dialog);
        info->ptMinTrackSize.x = ScaleForDpi(420, dpi);
        info->ptMinTrackSize.y = ScaleForDpi(300, dpi);
        return TRUE;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(l_param);
        SetWindowPos(dialog, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (state) {
            CreateSmallImageList(*state, HIWORD(w_param));
            state->rendered_rows.clear();
            RefreshVisibleList(dialog, *state);
            LayoutControls(dialog, *state);
        }
        return TRUE;
    }
    case WM_TIMER:
        if (state && w_param == kRefreshTimer) RequestRefresh(*state);
        return TRUE;
    case kRefreshMessage: {
        auto* update = reinterpret_cast<WorkerResult*>(l_param);
        if (state && update) {
            if (update->success) {
                RebuildList(dialog, *state, std::move(update->apps));
            } else if (!update->error.empty()) {
                OutputDebugStringW((L"MemPressMonitor capture failed: " + update->error + L"\n").c_str());
            }
        }
        delete update;
        return TRUE;
    }
    case WM_NOTIFY:
        if (state && reinterpret_cast<NMHDR*>(l_param)->idFrom == IDC_APP_LIST) {
            auto* header = reinterpret_cast<NMHDR*>(l_param);
            if (header->code == NM_CUSTOMDRAW) {
                SetWindowLongPtrW(dialog, DWLP_MSGRESULT, HandleCustomDraw(*state, l_param));
                return TRUE;
            }
            if (header->code == LVN_ITEMCHANGED)
                UpdateEndTaskState(dialog, *state);
            if (header->code == LVN_COLUMNCLICK) {
                const int column = reinterpret_cast<const NMLISTVIEW*>(l_param)->iSubItem;
                if (column == state->sort_column) {
                    state->sort_ascending = !state->sort_ascending;
                } else {
                    state->sort_column = column;
                    state->sort_ascending = column == 0;
                }
                UpdateSortArrows(*state);
                std::vector<memcore::AppEntry> apps = state->all_apps;
                RebuildList(dialog, *state, std::move(apps));
            }
        }
        break;
    case WM_COMMAND:
        if (LOWORD(w_param) == IDC_END_TASK && HIWORD(w_param) == BN_CLICKED && state) {
            EndSelectedTask(dialog, *state);
            return TRUE;
        }
        if (LOWORD(w_param) == IDC_EXPAND && HIWORD(w_param) == BN_CLICKED && state) {
            state->show_all = !state->show_all;
            UpdateExpandButton(dialog, *state);
            RefreshVisibleList(dialog, *state);
            return TRUE;
        }
        if (LOWORD(w_param) == IDC_PIN_TOPMOST && HIWORD(w_param) == BN_CLICKED) {
            const bool pinned = IsDlgButtonChecked(dialog, IDC_PIN_TOPMOST) == BST_CHECKED;
            SetWindowPos(dialog, pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE);
            return TRUE;
        }
        if (LOWORD(w_param) == IDCANCEL) {
            EndDialog(dialog, 0);
            return TRUE;
        }
        break;
    case WM_SETTINGCHANGE:
        if (state && l_param && wcscmp(reinterpret_cast<const wchar_t*>(l_param),
                                       L"ImmersiveColorSet") == 0)
            ApplyTheme(dialog, *state);
        return TRUE;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (state) {
            const HDC dc = reinterpret_cast<HDC>(w_param);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, state->dark ? RGB(245, 245, 245) : GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(dc, state->dark ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(state->background_brush);
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, 0);
        return TRUE;
    case WM_DESTROY:
        if (state) {
            RemoveWindowSubclass(state->list, ListSubclassProc, 1);
            StopWorker(dialog, *state);
            if (state->owns_background_brush && state->background_brush)
                DeleteObject(state->background_brush);
            DestroySmallImageList(*state);
            if (state->icon_big) DestroyIcon(state->icon_big);
            if (state->icon_small) DestroyIcon(state->icon_small);
            delete state;
            SetWindowLongPtrW(dialog, DWLP_USER, 0);
        }
        return TRUE;
    default:
        break;
    }
    return FALSE;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);
    return static_cast<int>(DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr,
                                             DialogProc, 0));
}
