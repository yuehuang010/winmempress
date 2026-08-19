// Test mock: shows one visible top-level window named after its own exe.
// Built as both mock-ui.exe and mock-game.exe so grouping tests get two
// distinct visible anchor exes from one source.
#include <windows.h>

namespace {
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"MemPressMockWindow";
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&window_class);

    const HWND window = CreateWindowExW(0, window_class.lpszClassName, name,
                                        WS_OVERLAPPEDWINDOW, 40, 40, 320, 160,
                                        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show ? show : SW_SHOWNORMAL);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
