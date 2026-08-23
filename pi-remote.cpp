#include <cstdlib>

int main()
{
    return std::system(
        "\"C:\\Windows\\System32\\OpenSSH\\ssh.exe\" "
        "pali@192.168.255.129 "
        "sudo /usr/local/bin/pi-rpc"
    );
}
