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
    auto* args = static_cast<ForwardArgs*>(param);

    char buffer[8192];
    DWORD bytesRead = 0;

    while (ReadFile(args->from, buffer, sizeof(buffer), &bytesRead, nullptr) &&
           bytesRead > 0)
    {
        DWORD totalWritten = 0;

        while (totalWritten < bytesRead)
        {
            DWORD bytesWritten = 0;

            if (!WriteFile(
                    args->to,
                    buffer + totalWritten,
                    bytesRead - totalWritten,
                    &bytesWritten,
                    nullptr))
            {
                if (args->closeToWhenDone)
                    CloseHandle(args->to);

                return 1;
            }

            totalWritten += bytesWritten;
        }
    }

    if (args->closeToWhenDone)
        CloseHandle(args->to);

    return 0;
}

int main()
{
    // Intentionally ignore every command-line argument.
    //
    // Pi Chat may launch this executable as:
    //
    //     pi-remote.exe --mode rpc
    //
    // None of those arguments are forwarded across the VM boundary.
    // The remote command is fixed by design.

    const wchar_t* sshPath =
        L"C:\\Windows\\System32\\OpenSSH\\ssh.exe";

    std::wstring command =
        L"ssh.exe "
        L"-T "
        L"-o BatchMode=yes "
        L"-o IdentitiesOnly=yes "
        L"-i C:\\Users\\Pali\\.ssh\\id_ed25519 "
        L"pali@192.168.255.129 "
        L"sudo /usr/local/bin/pi-rpc";

    // CreateProcessW requires a writable, null-terminated buffer.
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE sshStdinRead   = nullptr;
    HANDLE sshStdinWrite  = nullptr;
    HANDLE sshStdoutRead  = nullptr;
    HANDLE sshStdoutWrite = nullptr;
    HANDLE sshStderrRead  = nullptr;
    HANDLE sshStderrWrite = nullptr;

    if (!CreatePipe(&sshStdinRead, &sshStdinWrite, &sa, 0) ||
        !CreatePipe(&sshStdoutRead, &sshStdoutWrite, &sa, 0) ||
        !CreatePipe(&sshStderrRead, &sshStderrWrite, &sa, 0))
    {
        std::cerr << "pi-remote: CreatePipe failed; Win32 error="
                  << GetLastError() << '\n';
        return 100;
    }

    // These are used only by this bridge process and must not be inherited.
    SetHandleInformation(sshStdinWrite,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(sshStdoutRead,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(sshStderrRead,  HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = sshStdinRead;
    si.hStdOutput = sshStdoutWrite;
    si.hStdError  = sshStderrWrite;

    PROCESS_INFORMATION pi{};

    BOOL started = CreateProcessW(
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

    // Parent no longer needs the child-side handles.
    CloseHandle(sshStdinRead);
    CloseHandle(sshStdoutWrite);
    CloseHandle(sshStderrWrite);

    if (!started)
    {
        std::cerr << "pi-remote: failed to start ssh.exe; Win32 error="
                  << GetLastError() << '\n';

        CloseHandle(sshStdinWrite);
        CloseHandle(sshStdoutRead);
        CloseHandle(sshStderrRead);

        return 101;
    }

    ForwardArgs stdinArgs{
        GetStdHandle(STD_INPUT_HANDLE),
        sshStdinWrite,
        true
    };

    ForwardArgs stdoutArgs{
        sshStdoutRead,
        GetStdHandle(STD_OUTPUT_HANDLE),
        false
    };

    ForwardArgs stderrArgs{
        sshStderrRead,
        GetStdHandle(STD_ERROR_HANDLE),
        false
    };

    HANDLE stdinThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdinArgs, 0, nullptr);

    HANDLE stdoutThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdoutArgs, 0, nullptr);

    HANDLE stderrThread =
        CreateThread(nullptr, 0, ForwardBytes, &stderrArgs, 0, nullptr);

    if (!stdinThread || !stdoutThread || !stderrThread)
    {
        std::cerr << "pi-remote: CreateThread failed; Win32 error="
                  << GetLastError() << '\n';

        TerminateProcess(pi.hProcess, 102);
        WaitForSingleObject(pi.hProcess, INFINITE);

        if (stdinThread)  CloseHandle(stdinThread);
        if (stdoutThread) CloseHandle(stdoutThread);
        if (stderrThread) CloseHandle(stderrThread);

        CloseHandle(sshStdoutRead);
        CloseHandle(sshStderrRead);

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        return 102;
    }

    // Keep the bridge alive for the lifetime of the SSH/RPC session.
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;

    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        std::cerr << "pi-remote: failed to read ssh.exe exit code; Win32 error="
                  << GetLastError() << '\n';

        exitCode = 103;
    }

    // stdout/stderr should reach EOF once ssh.exe exits.
    WaitForSingleObject(stdoutThread, 2000);
    WaitForSingleObject(stderrThread, 2000);

    // Do not wait indefinitely for stdin: VS Code may keep the pipe open.
    CloseHandle(stdinThread);
    CloseHandle(stdoutThread);
    CloseHandle(stderrThread);

    CloseHandle(sshStdoutRead);
    CloseHandle(sshStderrRead);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return static_cast<int>(exitCode);
}
