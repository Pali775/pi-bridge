#define UNICODE
#define _UNICODE

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

struct ForwardArgs {
    HANDLE from;
    HANDLE to;
    bool closeToWhenDone;
};

DWORD WINAPI ForwardBytes(LPVOID param)
{
    ForwardArgs* args = static_cast<ForwardArgs*>(param);
    char buffer[8192];
    DWORD bytesRead = 0;

    while (ReadFile(args->from, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        DWORD totalWritten = 0;
        while (totalWritten < bytesRead) {
            DWORD bytesWritten = 0;
            if (!WriteFile(args->to, buffer + totalWritten,
                           bytesRead - totalWritten, &bytesWritten, nullptr)) {
                if (args->closeToWhenDone) CloseHandle(args->to);
                return 1;
            }
            totalWritten += bytesWritten;
        }
    }

    if (args->closeToWhenDone) CloseHandle(args->to);
    return 0;
}

int main()
{
    const wchar_t* sshPath =
        L"C:\\Windows\\System32\\OpenSSH\\ssh.exe";

    std::wstring command =
        L"ssh.exe -T -o BatchMode=yes "
        L"pali@192.168.255.129 "
        L"sudo /usr/local/bin/pi-rpc";

    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdinRead = nullptr, childStdinWrite = nullptr;
    HANDLE childStdoutRead = nullptr, childStdoutWrite = nullptr;
    HANDLE childStderrRead = nullptr, childStderrWrite = nullptr;

    if (!CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0) ||
        !CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0) ||
        !CreatePipe(&childStderrRead, &childStderrWrite, &sa, 0)) {
        std::cerr << "pi-remote: CreatePipe failed; Win32 error="
                  << GetLastError() << '\n';
        return 100;
    }

    SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(childStderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = childStderrWrite;

    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessW(
        sshPath,
        commandBuffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    CloseHandle(childStderrWrite);

    if (!ok) {
        std::cerr << "pi-remote: failed to start ssh.exe; Win32 error="
                  << GetLastError() << '\n';
        CloseHandle(childStdinWrite);
        CloseHandle(childStdoutRead);
        CloseHandle(childStderrRead);
        return 101;
    }

    ForwardArgs stdinArgs{GetStdHandle(STD_INPUT_HANDLE), childStdinWrite, true};
    ForwardArgs stdoutArgs{childStdoutRead, GetStdHandle(STD_OUTPUT_HANDLE), false};
    ForwardArgs stderrArgs{childStderrRead, GetStdHandle(STD_ERROR_HANDLE), false};

    HANDLE stdinThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdinArgs, 0, nullptr);
    HANDLE stdoutThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdoutArgs, 0, nullptr);
    HANDLE stderrThread =
        CreateThread(nullptr, 0, ForwardBytes, &stderrArgs, 0, nullptr);

    if (!stdinThread || !stdoutThread || !stderrThread) {
        std::cerr << "pi-remote: CreateThread failed; Win32 error="
                  << GetLastError() << '\n';
        TerminateProcess(pi.hProcess, 102);
        WaitForSingleObject(pi.hProcess, INFINITE);
        return 102;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    WaitForSingleObject(stdoutThread, 5000);
    WaitForSingleObject(stderrThread, 5000);

    CancelSynchronousIo(stdinThread);
    WaitForSingleObject(stdinThread, 1000);

    CloseHandle(stdinThread);
    CloseHandle(stdoutThread);
    CloseHandle(stderrThread);

    CloseHandle(childStdoutRead);
    CloseHandle(childStderrRead);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return static_cast<int>(exitCode);
}
