#define UNICODE
#define _UNICODE
#include <windows.h>
#include <string>
#include <iostream>

int wmain() {
    const wchar_t* sshPath = L"C:\\Windows\\System32\\OpenSSH\\ssh.exe";

    // Mutable command line required by CreateProcessW.
    std::wstring command =
        L"ssh.exe pali@192.168.255.129 sudo /usr/local/bin/pi-rpc";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    // Inherit this process' standard streams so JSONL passes through unchanged.
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessW(
        sshPath,
        command.data(),
        nullptr,
        nullptr,
        TRUE,               // inherit stdin/stdout/stderr handles
        0,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!ok) {
        std::wcerr << L"Failed to start ssh.exe. Win32 error: "
                   << GetLastError() << L"\n";
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return static_cast<int>(exitCode);
}
