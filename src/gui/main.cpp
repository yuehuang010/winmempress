#include "resource.h"
#include "grouper.h"
#include "pressure.h"
#include "snapshot.h"

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
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

struct DialogState {
    HWND dialog = nullptr;
    HWND list = nullptr;
    HANDLE stop_event = nullptr;
    HANDLE refresh_event = nullptr;
    std::thread worker;
    std::vector<memcore::AppEntry> apps;
    bool dark = false;
    HBRUSH background_brush = nullptr;
    bool owns_background_brush = false;
    int sort_column = 1;
    bool sort_ascending = false;
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
    for (const int control : {IDC_END_TASK, IDC_PIN_TOPMOST}) {
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
               ScaleForDpi(70, dpi), button_height, TRUE);
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

void RebuildList(HWND dialog, DialogState& state, std::vector<memcore::AppEntry> apps) {
    std::wstring selected_key;
    const int old_selected = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    if (old_selected >= 0 && static_cast<std::size_t>(old_selected) < state.apps.size())
        selected_key = state.apps[static_cast<std::size_t>(old_selected)].key;

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
    state.apps = std::move(apps);
    ListView_DeleteAllItems(state.list);

    for (std::size_t index = 0; index < state.apps.size(); ++index) {
        const auto& app = state.apps[index];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(index);
        item.lParam = static_cast<LPARAM>(index);
        item.pszText = const_cast<LPWSTR>(app.display_name.c_str());
        ListView_InsertItem(state.list, &item);

        const std::wstring memory = FormatMemory(app.working_set);
        ListView_SetItemText(state.list, static_cast<int>(index), 1,
                             const_cast<LPWSTR>(memory.c_str()));
        const std::wstring pressure = std::to_wstring(app.pressure.value) + L"%";
        ListView_SetItemText(state.list, static_cast<int>(index), 2,
                             const_cast<LPWSTR>(pressure.c_str()));

        if (app.key == selected_key) {
            ListView_SetItemState(state.list, static_cast<int>(index),
                                  LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
    UpdateEndTaskState(dialog, state);
    InvalidateRect(state.list, nullptr, TRUE);
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
    if (MessageBoxW(dialog, message.c_str(), L"WinMemPress",
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
    case memcore::PressureBand::Low: return dark ? RGB(110, 220, 125) : RGB(0, 120, 0);
    case memcore::PressureBand::Moderate: return dark ? RGB(255, 210, 80) : RGB(145, 95, 0);
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
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(new_state));
        ListView_SetExtendedListViewStyle(new_state->list,
                                          LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        AddColumns(new_state->list);
        SetWindowSubclass(new_state->list, ListSubclassProc, 1,
                          reinterpret_cast<DWORD_PTR>(new_state));
        UpdateSortArrows(*new_state);
        ApplyTheme(dialog, *new_state);
        LayoutControls(dialog, *new_state);
        EnableWindow(GetDlgItem(dialog, IDC_END_TASK), FALSE);
        if (!StartWorker(*new_state)) {
            MessageBoxW(dialog, L"Unable to start the memory capture worker.", L"WinMemPress",
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
        if (state) LayoutControls(dialog, *state);
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
                OutputDebugStringW((L"WinMemPress capture failed: " + update->error + L"\n").c_str());
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
                std::vector<memcore::AppEntry> apps = state->apps;
                RebuildList(dialog, *state, std::move(apps));
            }
        }
        break;
    case WM_COMMAND:
        if (LOWORD(w_param) == IDC_END_TASK && HIWORD(w_param) == BN_CLICKED && state) {
            EndSelectedTask(dialog, *state);
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
