#define UNICODE
#define _UNICODE

#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static void logLine(const std::string& text)
{
    std::ofstream f("C:\\pi-bridge\\bridge.log", std::ios::app | std::ios::binary);
    if (f)
        f << text << "\r\n";
}

struct ForwardArgs
{
    HANDLE from;
    HANDLE to;
    bool closeDestinationWhenDone;
    const char* name;
};

DWORD WINAPI ForwardBytes(LPVOID param)
{
    auto* args = static_cast<ForwardArgs*>(param);

    char buffer[8192];
    DWORD bytesRead = 0;

    while (ReadFile(args->from, buffer, sizeof(buffer), &bytesRead, nullptr) &&
           bytesRead > 0)
    {
        DWORD offset = 0;

        while (offset < bytesRead)
        {
            DWORD bytesWritten = 0;

            if (!WriteFile(
                    args->to,
                    buffer + offset,
                    bytesRead - offset,
                    &bytesWritten,
                    nullptr))
            {
                logLine(std::string(args->name) +
                        ": WriteFile failed; error=" +
                        std::to_string(GetLastError()));

                if (args->closeDestinationWhenDone)
                    CloseHandle(args->to);

                return 1;
            }

            offset += bytesWritten;
        }
    }

    logLine(std::string(args->name) + ": forwarding ended");

    if (args->closeDestinationWhenDone)
        CloseHandle(args->to);

    return 0;
}

int main()
{
    logLine("========================================");
    logLine("bridge started");
    logLine("command-line arguments intentionally ignored");

    // Pi Chat may launch:
    //
    //   pi-remote.exe --mode rpc
    //
    // All CLI arguments are intentionally ignored.
    //
    // The dedicated SSH key is restricted server-side with a forced command,
    // so this bridge only opens the SSH connection. The Debian SSH server
    // decides which command is executed.

    const wchar_t* sshPath =
        L"C:\\Windows\\System32\\OpenSSH\\ssh.exe";

    std::wstring command =
        L"ssh.exe "
        L"-T "
        L"-o BatchMode=yes "
        L"-o IdentitiesOnly=yes "
        L"-i C:\\Users\\Pali\\.ssh\\pi_bridge "
        L"pali@192.168.255.129";

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

    logLine("creating SSH pipes");

    if (!CreatePipe(&sshStdinRead, &sshStdinWrite, &sa, 0) ||
        !CreatePipe(&sshStdoutRead, &sshStdoutWrite, &sa, 0) ||
        !CreatePipe(&sshStderrRead, &sshStderrWrite, &sa, 0))
    {
        logLine("CreatePipe failed; error=" +
                std::to_string(GetLastError()));
        return 100;
    }

    // Bridge-side pipe ends must not be inherited by ssh.exe.
    SetHandleInformation(sshStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(sshStdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(sshStderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = sshStdinRead;
    si.hStdOutput = sshStdoutWrite;
    si.hStdError  = sshStderrWrite;

    PROCESS_INFORMATION pi{};

    logLine("starting ssh.exe");

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

    // The bridge no longer needs the child-side handles.
    CloseHandle(sshStdinRead);
    CloseHandle(sshStdoutWrite);
    CloseHandle(sshStderrWrite);

    if (!started)
    {
        DWORD error = GetLastError();
        logLine("CreateProcessW failed; error=" + std::to_string(error));

        CloseHandle(sshStdinWrite);
        CloseHandle(sshStdoutRead);
        CloseHandle(sshStderrRead);

        return 101;
    }

    logLine("ssh.exe started; PID=" + std::to_string(pi.dwProcessId));

    HANDLE bridgeStdin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE bridgeStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE bridgeStderr = GetStdHandle(STD_ERROR_HANDLE);

    ForwardArgs stdinArgs{
        bridgeStdin,
        sshStdinWrite,
        true,
        "stdin->ssh"
    };

    ForwardArgs stdoutArgs{
        sshStdoutRead,
        bridgeStdout,
        false,
        "ssh->stdout"
    };

    ForwardArgs stderrArgs{
        sshStderrRead,
        bridgeStderr,
        false,
        "ssh->stderr"
    };

    HANDLE stdinThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdinArgs, 0, nullptr);

    HANDLE stdoutThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdoutArgs, 0, nullptr);

    HANDLE stderrThread =
        CreateThread(nullptr, 0, ForwardBytes, &stderrArgs, 0, nullptr);

    if (!stdinThread || !stdoutThread || !stderrThread)
    {
        DWORD error = GetLastError();
        logLine("CreateThread failed; error=" + std::to_string(error));

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

    logLine("stdio forwarding active");
    logLine("waiting for ssh.exe");

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 999;

    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        logLine("GetExitCodeProcess failed; error=" +
                std::to_string(GetLastError()));
        exitCode = 103;
    }
    else
    {
        logLine("ssh.exe exit code=" + std::to_string(exitCode));
    }

    // ssh.exe closing its pipes should let stdout/stderr threads finish.
    WaitForSingleObject(stdoutThread, 2000);
    WaitForSingleObject(stderrThread, 2000);

    // Do not block waiting for stdin; VS Code may keep that pipe open.
    CloseHandle(stdinThread);
    CloseHandle(stdoutThread);
    CloseHandle(stderrThread);

    CloseHandle(sshStdoutRead);
    CloseHandle(sshStderrRead);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    logLine("bridge exiting with code=" + std::to_string(exitCode));
    logLine("========================================");

    return static_cast<int>(exitCode);
}
