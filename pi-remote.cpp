#define UNICODE
#define _UNICODE

#include <windows.h>
#include <iostream>
#include <string>

int wmain()
{
    const wchar_t* sshPath =
        L"C:\\Windows\\System32\\OpenSSH\\ssh.exe";

    // CreateProcessW requires a writable command-line buffer.
    std::wstring command =
        L"ssh.exe "
        L"pali@192.168.255.129 "
        L"sudo /usr/local/bin/pi-rpc";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    // Pass Pi Chat's JSONL streams straight through to SSH.
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessW(
        sshPath,          // absolute executable path
        command.data(),   // mutable command line
        nullptr,
        nullptr,
        TRUE,             // inherit stdin/stdout/stderr handles
        CREATE_NO_WINDOW, // no extra console window
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!ok)
    {
        DWORD error = GetLastError();
        std::wcerr
            << L"pi-remote: failed to start ssh.exe; Win32 error="
            << error << L"\n";
        return 100;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        DWORD error = GetLastError();
        std::wcerr
            << L"pi-remote: failed to read ssh.exe exit code; Win32 error="
            << error << L"\n";
        exitCode = 101;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return static_cast<int>(exitCode);
}
