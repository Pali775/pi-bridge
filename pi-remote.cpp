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
    if (f) f << text << "\r\n";
}

struct ForwardArgs {
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
            if (!WriteFile(args->to, buffer + offset, bytesRead - offset,
                           &bytesWritten, nullptr))
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

    const wchar_t* sshPath =
        L"C:\\Windows\\System32\\OpenSSH\\ssh.exe";

    // Everything important is explicit so the VS Code Extension Host
    // environment cannot silently change SSH behaviour.
    std::wstring command =
        L"ssh.exe "
        L"-T "
        L"-F NUL "
        L"-o BatchMode=yes "
        L"-o IdentitiesOnly=yes "
        L"-o StrictHostKeyChecking=yes "
        L"-o UserKnownHostsFile=C:\\Users\\Pali\\.ssh\\known_hosts "
        L"-i C:\\Users\\Pali\\.ssh\\pi_bridge "
        L"pali@192.168.255.129";

    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE sshStdinRead = nullptr, sshStdinWrite = nullptr;
    HANDLE sshStdoutRead = nullptr, sshStdoutWrite = nullptr;

    if (!CreatePipe(&sshStdinRead, &sshStdinWrite, &sa, 0) ||
        !CreatePipe(&sshStdoutRead, &sshStdoutWrite, &sa, 0))
    {
        logLine("CreatePipe failed; error=" + std::to_string(GetLastError()));
        return 100;
    }

    SetHandleInformation(sshStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(sshStdoutRead, HANDLE_FLAG_INHERIT, 0);

    // IMPORTANT: SSH stderr goes straight to a real file handle.
    // This bypasses Pi Chat / VS Code stderr handling completely.
    HANDLE sshErrorFile = CreateFileW(
        L"C:\\pi-bridge\\ssh-stderr.log",
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (sshErrorFile == INVALID_HANDLE_VALUE)
    {
        logLine("CreateFile(ssh-stderr.log) failed; error=" +
                std::to_string(GetLastError()));
        return 101;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = sshStdinRead;
    si.hStdOutput = sshStdoutWrite;
    si.hStdError  = sshErrorFile;

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

    CloseHandle(sshStdinRead);
    CloseHandle(sshStdoutWrite);
    CloseHandle(sshErrorFile);

    if (!started)
    {
        logLine("CreateProcessW failed; error=" +
                std::to_string(GetLastError()));
        CloseHandle(sshStdinWrite);
        CloseHandle(sshStdoutRead);
        return 102;
    }

    logLine("ssh.exe started; PID=" + std::to_string(pi.dwProcessId));

    ForwardArgs stdinArgs{
        GetStdHandle(STD_INPUT_HANDLE),
        sshStdinWrite,
        true,
        "stdin->ssh"
    };

    ForwardArgs stdoutArgs{
        sshStdoutRead,
        GetStdHandle(STD_OUTPUT_HANDLE),
        false,
        "ssh->stdout"
    };

    HANDLE stdinThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdinArgs, 0, nullptr);

    HANDLE stdoutThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdoutArgs, 0, nullptr);

    if (!stdinThread || !stdoutThread)
    {
        logLine("CreateThread failed; error=" +
                std::to_string(GetLastError()));
        TerminateProcess(pi.hProcess, 103);
        WaitForSingleObject(pi.hProcess, INFINITE);
        return 103;
    }

    logLine("stdio forwarding active");
    logLine("waiting for ssh.exe");

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 999;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    logLine("ssh.exe exit code=" + std::to_string(exitCode));

    WaitForSingleObject(stdoutThread, 2000);

    CloseHandle(stdinThread);
    CloseHandle(stdoutThread);
    CloseHandle(sshStdoutRead);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    logLine("bridge exiting with code=" + std::to_string(exitCode));
    logLine("========================================");

    return static_cast<int>(exitCode);
}
