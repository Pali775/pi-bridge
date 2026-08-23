# pi-bridge

A small experimental Windows bridge for connecting **Pi Chat in Visual Studio Code** to a **Pi coding agent running inside a remote or isolated Linux environment**.

> **Status:** Experimental / archived proof of concept  
> This repository is preserved because the approach works at the protocol level and may be worth revisiting, but it is not currently intended as a production-ready integration.

## Background

This project grew out of my experiments with running an agentic coding environment locally while keeping the agent isolated from the Windows host.

The original experiment is described here:

**[Running a Coding LLM Locally — and Why I Put the AI in a Cage](https://devntestblog.wordpress.com/2026/08/21/running-a-coding-llm-locally-and-why-i-put-the-ai-in-a-cage/)**

The environment used:

- Windows as the host operating system
- VMware as the virtualisation boundary
- Debian as the isolated guest
- Docker as an additional execution boundary
- Pi as the agentic coding layer
- llama.cpp as the local inference server
- Qwen as the local language model

The next challenge was connecting that isolated Pi environment to Visual Studio Code without moving the agent itself onto the Windows host.

That led to `pi-bridge`.

## The Problem

Pi Chat for VS Code normally expects Pi to be available as a local executable.

Conceptually:

    VS Code
        |
        v
    Pi Chat
        |
        | JSONL over stdin/stdout
        v
    Pi --mode rpc

In my setup, however, Pi deliberately runs inside an isolated Debian VM.

The required architecture therefore looked more like this:

    VS Code / Pi Chat
            |
            | JSONL over stdin/stdout
            v
       pi-bridge.exe
            |
            | SSH
            v
        Debian VM
            |
            v
          Pi RPC
            |
            | OpenAI-compatible HTTP/JSON
            v
        llama.cpp
            |
            v
          Qwen

The purpose of `pi-bridge.exe` is to make the remote Pi process appear to Pi Chat as if it were a local stdin/stdout RPC process.

## Concept

`pi-bridge` is intentionally small.

Its job is essentially:

1. Start the Windows OpenSSH client.
2. Establish an SSH connection to the Debian VM.
3. Start or reach Pi in RPC mode on the remote side.
4. Forward Pi Chat's stdin to the SSH process.
5. Forward the SSH process's stdout back to Pi Chat.
6. Preserve the JSONL stream expected by the Pi RPC protocol.
7. Propagate process termination and useful diagnostic information.

In simplified form:

    Pi Chat
       |
       | stdin
       v
    +-----------+
    | pi-bridge |
    +-----------+
       |
       | stdin
       v
     ssh.exe
       |
       | SSH
       v
    Debian VM
       |
       v
    Pi --mode rpc

Responses travel through the same chain in reverse.

The bridge does **not** interpret the model output and is not intended to implement the Pi protocol itself. It acts as a transport adapter between a local Windows process interface and a remote Pi RPC process.

## Why SSH?

SSH provides several useful properties for this architecture:

- encrypted transport;
- public-key authentication;
- mature Windows and Linux implementations;
- no need to expose Pi RPC directly over the network;
- server-side restrictions can limit what a dedicated key is allowed to execute.

In the wider experiment, a dedicated SSH identity was used rather than a normal administrative key.

A restricted `authorized_keys` entry can also associate that identity with a forced command, allowing the key to launch only the Pi RPC entry point instead of providing a general-purpose interactive shell.

## Why This Repository Is Archived

The bridge successfully demonstrated that Pi's JSONL RPC traffic could be transported between Windows and the isolated Debian environment.

However, making the bridge behave exactly like a locally spawned Pi process inside the VS Code Extension Host introduced additional complexity around:

- Windows pipes;
- stdin/stdout forwarding;
- process handle inheritance;
- `CreateProcessW`;
- OpenSSH process behaviour;
- child-process lifetime management;
- EOF propagation;
- differences between interactive PowerShell execution and execution from the VS Code Extension Host.

During the experiment I eventually adopted a simpler approach: modifying Pi Chat so that it launches the existing Windows `ssh.exe` directly.

In other words, instead of:

    Pi Chat
       |
       v
    pi-bridge.exe
       |
       v
    ssh.exe

the working implementation became:

    Pi Chat
       |
       v
    ssh.exe
       |
       v
    Debian / Pi RPC

This removed an entire process and pipe-management layer.

Nevertheless, `pi-bridge` is preserved here because the concept remains useful and the implementation may be worth revisiting.

If Windows process management, pipes, SSH and RPC transport sound like an interesting challenge, feel free to continue where I stopped.

## Building

### Requirements

The bridge is written in C++ for Windows.

A typical build environment requires:

- Windows 10/11
- Visual Studio Build Tools or Visual Studio with C++ support
- MSVC
- Windows SDK
- OpenSSH Client (`ssh.exe`)

Verify that OpenSSH is available:

    C:\Windows\System32\OpenSSH\ssh.exe -V

### Compile with MSVC

Open a **Developer Command Prompt for Visual Studio** and compile the source using `cl.exe`.

For a simple single-source implementation, this will typically look like:

    cl /EHsc /std:c++17 /O2 pi-bridge.cpp /Fe:pi-bridge.exe

Adjust the source filename if the repository uses a different layout.

The resulting executable should be:

    pi-bridge.exe

### Debug Build

For development/debugging, optimisation can be disabled and debug information enabled:

    cl /EHsc /std:c++17 /Zi /Od pi-bridge.cpp /Fe:pi-bridge.exe

## Testing

Before attempting integration with VS Code, test the SSH path independently.

For example:

    ssh -T -o BatchMode=yes -o IdentitiesOnly=yes ^
        -i C:\Users\<USER>\.ssh\pi_bridge ^
        <USER>@<DEBIAN-IP>

The remote side should start the intended Pi RPC endpoint rather than an interactive shell.

The bridge should then be tested with simple Pi RPC messages before attempting longer agentic interactions.

A minimal test prompt is preferable to code generation during initial diagnostics.

For example:

    Reply with exactly: PIPE BRIDGE WORKS

This makes it easier to distinguish transport problems from model, context, tool or resource problems.

## Security Notes

This project exists specifically because the Pi agent was intentionally kept away from the Windows host.

Do not treat SSH alone as a sandbox.

A more defensible deployment should consider:

- a dedicated SSH key for the bridge;
- public-key authentication;
- `BatchMode=yes`;
- `IdentitiesOnly=yes`;
- a server-side forced command;
- disabling unnecessary SSH capabilities;
- restricting Pi's available tools;
- exposing only explicitly selected project directories;
- keeping administrative SSH credentials separate;
- limiting network access where practical;
- running Pi inside an additional container or sandbox;
- avoiding unnecessary host environment variables or credentials.

The principle is simple:

> Give the agent only the capabilities required for the task.

## Important Distinction: Authentication vs Authorisation

The SSH key authenticates the client.

It does **not**, by itself, determine that Pi should run in RPC mode.

That restriction belongs on the server side.

For example, a dedicated public key can be associated with a forced command in `authorized_keys`:

    restrict,command="sudo /usr/local/bin/pi-rpc" ssh-ed25519 AAAA... pi-vscode-bridge

The resulting chain is:

    dedicated SSH key
            |
            v
      authentication
            |
            v
    authorized_keys policy
            |
            v
       forced command
            |
            v
    /usr/local/bin/pi-rpc
            |
            v
       Pi --mode rpc

This separates **authentication**, **authorisation**, and **process execution**.

## Related Projects

### Pi

Pi provides the agentic coding layer used in this experiment.

### Pi Chat for VS Code

Pi Chat provides the VS Code user interface and communicates with Pi using its RPC interface:

https://github.com/iqbalabiyoga/pi-vscode-chat

### llama.cpp

llama.cpp provides the local inference server used by Pi:

https://github.com/ggml-org/llama.cpp

## Limitations

This repository should currently be considered a proof of concept.

Known areas requiring additional work include:

- robust bidirectional pipe handling;
- graceful EOF handling;
- child-process cleanup;
- SSH error propagation;
- connection recovery;
- timeout handling;
- structured logging;
- VS Code Extension Host compatibility;
- configuration instead of hard-coded paths;
- automated tests;
- security hardening.

It has **not** been audited for production use.

## Possible Future Work

If I return to this project, the most interesting improvements would be:

- configurable SSH host, user and identity;
- clean separation between transport and process management;
- asynchronous Windows I/O;
- reliable shutdown propagation;
- reconnect support;
- structured diagnostic logging;
- automated RPC tests;
- minimal environment propagation;
- removal of hard-coded machine-specific values;
- packaging as a reusable transport layer rather than a one-off executable.

## Repository Purpose

This repository is primarily an archive of an engineering experiment.

It represents one attempted solution to a broader question:

> How can an agentic coding system running inside an isolated environment be integrated into a developer's IDE without moving the agent across the security boundary?

The final experiment found a simpler answer by modifying Pi Chat to use OpenSSH directly.

But sometimes the discarded path contains interesting engineering problems of its own.

That is why this code is still here.

## Disclaimer

This is experimental software.

It interacts with SSH, process execution and an agentic AI system capable of using tools and accessing files. Review the source, understand the permissions involved, and adapt the security controls to your own environment before using it with valuable systems or data.

## License

No licence has been selected yet.

If this repository is intended for public reuse, add an explicit open-source licence before distributing or accepting contributions.

