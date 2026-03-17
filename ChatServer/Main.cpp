#include <cstdint>
#include <iostream>
#include <string>

#include "Server.h"

int main(int argc, char* argv[])
{
    std::uint16_t port = 7777;
    if (argc >= 2)
    {
        port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    Server server;
    if (!server.Start(port))
    {
        std::cerr << "Failed to start IOCP chat server on port " << port << std::endl;
        return 1;
    }

    std::cout << "IOCP chat server is running on port " << port << std::endl;
    std::cout << "Press ENTER to stop the server." << std::endl;

    std::string line;
    std::getline(std::cin, line);

    server.Stop();
    return 0;
}
