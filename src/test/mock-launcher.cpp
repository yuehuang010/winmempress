// Test mock: windowless launcher reproducing the Steam pattern — it spawns
// mock-ui.exe (the app's own UI) and, a few seconds later, mock-game.exe
// (a separately launched app), then idles. Grouping should anchor each
// visible child as its own app and send this process to Background & OS.
#include <windows.h>
#include <string>

namespace {
bool Launch(const std::wstring& path) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + path + L"\"";
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process))
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring directory(path);
    const std::size_t slash = directory.find_last_of(L'\\');
    if (slash == std::wstring::npos) return 1;
    directory.resize(slash + 1);

    if (!Launch(directory + L"mock-ui.exe")) return 1;
    Sleep(3000);
    if (!Launch(directory + L"mock-game.exe")) return 1;
    Sleep(INFINITE);
    return 0;
}
