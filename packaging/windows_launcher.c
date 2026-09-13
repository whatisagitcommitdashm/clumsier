#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>

// Keep the download's top level simple without changing Qt's DLL search rules.
// This small, statically linked launcher opens the real app beside its DLLs.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show) {
    wchar_t directory[32768], executable[32768];
    DWORD length = GetModuleFileNameW(NULL, directory, 32768);
    SHELLEXECUTEINFOW launch = {0};
    BOOL check = lstrcmpW(arguments, L"--check-load") == 0;
    (void)instance; (void)previous; (void)show;
    if (!length || length >= 32768 - 32) return 1;
    while (length && directory[length - 1] != L'\\') --length;
    directory[length] = 0;
    lstrcatW(directory, L"app");
    lstrcpyW(executable, directory);
    lstrcatW(executable, L"\\clumsier-beta.exe");
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch.lpFile = executable;
    launch.lpParameters = arguments;
    launch.lpDirectory = directory;
    launch.nShow = check ? SW_HIDE : SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch)) {
        if (GetLastError() != ERROR_CANCELLED && !check)
            MessageBoxW(NULL, L"Clumsier could not start. Extract the whole ZIP and keep the app folder beside this launcher. If a runtime is missing, run app\\vc_redist.x64.exe.", L"Clumsier", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (check && launch.hProcess) {
        DWORD result = 1;
        if (WaitForSingleObject(launch.hProcess, 30000) == WAIT_OBJECT_0)
            GetExitCodeProcess(launch.hProcess, &result);
        else TerminateProcess(launch.hProcess, 1);
        CloseHandle(launch.hProcess);
        return (int)result;
    }
    if (launch.hProcess) CloseHandle(launch.hProcess);
    return 0;
}
