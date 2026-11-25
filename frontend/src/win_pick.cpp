// The Windows Open dialog for the first-run ROM pick (SDL's async dialog came back empty on a player's PC).
// Kept apart so <windows.h> stays out of main.cpp.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include <string>

namespace ph {

// UTF-8 path of the picked file; "" when cancelled. On a dialog failure, error gets a message.
std::string win_pick_rom(std::string& error) {
    wchar_t file[4096] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Nintendo DS ROM (*.nds)\0*.nds\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = DWORD(sizeof(file) / sizeof(file[0]));
    ofn.lpstrTitle = L"Select your Phantom Hourglass ROM";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) {
        const DWORD code = CommDlgExtendedError();
        if (code) error = "The file dialog failed (code " + std::to_string(code) + ").";
        return "";
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// UTF-8 path of the running exe.
std::string win_exe_path() {
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return "";
    const int m = WideCharToMultiByte(CP_UTF8, 0, buf, int(n), nullptr, 0, nullptr, nullptr);
    std::string out(m > 0 ? m : 0, '\0');
    if (m > 0) WideCharToMultiByte(CP_UTF8, 0, buf, int(n), out.data(), m, nullptr, nullptr);
    return out;
}

}  // namespace ph
#endif
