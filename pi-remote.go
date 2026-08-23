package main

import (
    "os"
    "os/exec"
)

func main() {
    // Intentionally ignore arguments added by the VS Code extension.
    // The remote root-owned launcher already starts Pi in the required RPC mode.
    ssh := `C:\Windows\System32\OpenSSH\ssh.exe`
    cmd := exec.Command(ssh, "pali@192.168.255.129", "sudo", "/usr/local/bin/pi-rpc")

    // Transparent JSONL transport: VS Code <-> ssh <-> remote Pi RPC.
    cmd.Stdin = os.Stdin
    cmd.Stdout = os.Stdout
    cmd.Stderr = os.Stderr

    if err := cmd.Run(); err != nil {
        if exitErr, ok := err.(*exec.ExitError); ok {
            os.Exit(exitErr.ExitCode())
        }
        os.Exit(1)
    }
}
