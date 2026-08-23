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

int main()
{
    logLine("========================================");
    logLine("bridge started");
    logLine("command-line arguments intentionally ignored");

    // Pi Chat may invoke:
    //
    //   pi-remote.exe --mode rpc
    //
    // All arguments are intentionally ignored.
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

    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    HANDLE parentStdin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE parentStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE parentStderr = GetStdHandle(STD_ERROR_HANDLE);

    logLine("stdin handle=" +
            std::to_string(reinterpret_cast<uintptr_t>(parentStdin)));
    logLine("stdout handle=" +
            std::to_string(reinterpret_cast<uintptr_t>(parentStdout)));
    logLine("stderr handle=" +
            std::to_string(reinterpret_cast<uintptr_t>(parentStderr)));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    // Directly give ssh.exe the exact stdin/stdout/stderr handles that
    // Pi Chat gave to this bridge process.
    si.hStdInput  = parentStdin;
    si.hStdOutput = parentStdout;
    si.hStdError  = parentStderr;

    PROCESS_INFORMATION pi{};

    logLine("calling CreateProcessW for ssh.exe");

    BOOL started = CreateProcessW(
        sshPath,
        commandBuffer.data(),
        nullptr,
        nullptr,
        TRUE,               // inherit the stdio handles above
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!started)
    {
        DWORD error = GetLastError();
        logLine("CreateProcessW FAILED; error=" + std::to_string(error));

        std::cerr << "pi-remote: failed to start ssh.exe; Win32 error="
                  << error << '\n';

        return 101;
    }

    logLine("ssh.exe started successfully");
    logLine("ssh PID=" + std::to_string(pi.dwProcessId));
    logLine("waiting for ssh.exe");

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 999;

    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        DWORD error = GetLastError();
        logLine("GetExitCodeProcess FAILED; error=" + std::to_string(error));

        std::cerr << "pi-remote: failed to read ssh.exe exit code; Win32 error="
                  << error << '\n';

        exitCode = 103;
    }
    else
    {
        logLine("ssh.exe exit code=" + std::to_string(exitCode));
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    logLine("bridge exiting with code=" + std::to_string(exitCode));
    logLine("========================================");

    return static_cast<int>(exitCode);
}
