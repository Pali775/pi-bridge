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

struct ForwardArgs {
    HANDLE from;
    HANDLE to;
    bool closeToWhenDone;
    const char* name;
    const char* teeFile; // optional extra log file
};

DWORD WINAPI ForwardBytes(LPVOID param)
{
    auto* args = static_cast<ForwardArgs*>(param);

    std::ofstream tee;
    if (args->teeFile != nullptr) {
        tee.open(args->teeFile, std::ios::app | std::ios::binary);
    }

    char buffer[8192];
    DWORD bytesRead = 0;

    while (ReadFile(args->from, buffer, sizeof(buffer), &bytesRead, nullptr) &&
           bytesRead > 0)
    {
        if (tee.is_open()) {
            tee.write(buffer, bytesRead);
            tee.flush();
        }

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
                logLine(std::string(args->name) +
                        ": WriteFile failed; error=" +
                        std::to_string(GetLastError()));

                if (args->closeToWhenDone)
                    CloseHandle(args->to);

                return 1;
            }

            totalWritten += bytesWritten;
        }
    }

    DWORD readError = GetLastError();

    logLine(std::string(args->name) +
            ": forwarding ended; ReadFile error=" +
            std::to_string(readError));

    if (args->closeToWhenDone)
        CloseHandle(args->to);

    return 0;
}

int main()
{
    logLine("========================================");
    logLine("bridge started");
    logLine("command-line arguments intentionally ignored");

    // Pi Chat may invoke:
    //
    //   pi-remote.exe --mode rpc
    //
    // All command-line arguments are intentionally ignored.
    // Nothing received as a CLI argument is forwarded across the VM boundary.

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

    logLine("creating pipes");

    if (!CreatePipe(&sshStdinRead, &sshStdinWrite, &sa, 0) ||
        !CreatePipe(&sshStdoutRead, &sshStdoutWrite, &sa, 0) ||
        !CreatePipe(&sshStderrRead, &sshStderrWrite, &sa, 0))
    {
        DWORD error = GetLastError();
        logLine("CreatePipe failed; error=" + std::to_string(error));
        return 100;
    }

    logLine("pipes created");

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

    logLine("calling CreateProcessW for ssh.exe");

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

    DWORD createProcessError = started ? ERROR_SUCCESS : GetLastError();

    CloseHandle(sshStdinRead);
    CloseHandle(sshStdoutWrite);
    CloseHandle(sshStderrWrite);

    if (!started)
    {
        logLine("CreateProcessW FAILED; error=" +
                std::to_string(createProcessError));

        CloseHandle(sshStdinWrite);
        CloseHandle(sshStdoutRead);
        CloseHandle(sshStderrRead);

        return 101;
    }

    logLine("ssh.exe started successfully");
    logLine("ssh PID=" + std::to_string(pi.dwProcessId));

    HANDLE parentStdin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE parentStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE parentStderr = GetStdHandle(STD_ERROR_HANDLE);

    ForwardArgs stdinArgs{
        parentStdin,
        sshStdinWrite,
        true,
        "stdin->ssh",
        nullptr
    };

    ForwardArgs stdoutArgs{
        sshStdoutRead,
        parentStdout,
        false,
        "ssh->stdout",
        nullptr
    };

    ForwardArgs stderrArgs{
        sshStderrRead,
        parentStderr,
        false,
        "ssh->stderr",
        "C:\\pi-bridge\\ssh-stderr.log"
    };

    logLine("starting forwarding threads");

    HANDLE stdinThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdinArgs, 0, nullptr);

    HANDLE stdoutThread =
        CreateThread(nullptr, 0, ForwardBytes, &stdoutArgs, 0, nullptr);

    HANDLE stderrThread =
        CreateThread(nullptr, 0, ForwardBytes, &stderrArgs, 0, nullptr);

    if (!stdinThread || !stdoutThread || !stderrThread)
    {
        DWORD error = GetLastError();
        logLine("CreateThread FAILED; error=" + std::to_string(error));

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

    logLine("forwarding threads started");
    logLine("waiting for ssh.exe");

    WaitForSingleObject(pi.hProcess, INFINITE);

    logLine("ssh.exe terminated");

    DWORD exitCode = 999;

    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        DWORD error = GetLastError();
        logLine("GetExitCodeProcess FAILED; error=" + std::to_string(error));
        exitCode = 103;
    }
    else
    {
        logLine("ssh.exe exit code=" + std::to_string(exitCode));
    }

    DWORD stdoutWait = WaitForSingleObject(stdoutThread, 2000);
    DWORD stderrWait = WaitForSingleObject(stderrThread, 2000);

    logLine("stdout thread wait result=" + std::to_string(stdoutWait));
    logLine("stderr thread wait result=" + std::to_string(stderrWait));

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
