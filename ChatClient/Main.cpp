#include <winsock2.h>
#include <WS2tcpip.h>

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../Common/Protocol.h"
#include "../ChatServer/PacketParser.h"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
bool SendPacket(SOCKET socket, const std::vector<char>& packet)
{
    int sent = 0;
    while (sent < static_cast<int>(packet.size()))
    {
        const int result = send(socket, packet.data() + sent, static_cast<int>(packet.size()) - sent, 0);
        if (result <= 0)
        {
            return false;
        }
        sent += result;
    }

    return true;
}

void PrintHelp()
{
    std::cout << "Commands:\n"
              << "  /nick <name>\n"
              << "  /create <room>\n"
              << "  /join <room>\n"
              << "  /leave\n"
              << "  /quit\n"
              << "Any other input is sent as a chat message.\n";
}
} // namespace

int main(int argc, char* argv[])
{
    const char* host = argc >= 2 ? argv[1] : "127.0.0.1";
    const char* port = argc >= 3 ? argv[2] : "7777";

    WSADATA wsaData {};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    if (getaddrinfo(host, port, &hints, &result) != 0)
    {
        std::cerr << "getaddrinfo failed" << std::endl;
        WSACleanup();
        return 1;
    }

    SOCKET socketHandle = INVALID_SOCKET;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
    {
        socketHandle = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (socketHandle == INVALID_SOCKET)
        {
            continue;
        }

        if (connect(socketHandle, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0)
        {
            break;
        }

        closesocket(socketHandle);
        socketHandle = INVALID_SOCKET;
    }

    freeaddrinfo(result);

    if (socketHandle == INVALID_SOCKET)
    {
        std::cerr << "Failed to connect to server" << std::endl;
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to " << host << ":" << port << std::endl;
    PrintHelp();

    std::atomic<bool> running = true;
    std::thread receiver([&]() {
        PacketParser parser;
        std::vector<char> buffer(4096);

        while (running.load())
        {
            const int received = recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received <= 0)
            {
                running = false;
                break;
            }

            std::vector<std::vector<char>> packets;
            std::string error;
            if (!parser.Append(buffer.data(), static_cast<std::size_t>(received), packets, error))
            {
                std::cerr << "Packet parse failed: " << error << std::endl;
                running = false;
                break;
            }

            for (const auto& packet : packets)
            {
                chat::PacketHeader header {};
                if (!chat::ReadHeader(packet.data(), packet.size(), header))
                {
                    continue;
                }

                chat::BufferReader reader(packet.data() + chat::kHeaderSize, packet.size() - chat::kHeaderSize);
                const auto type = static_cast<chat::PacketType>(header.type);

                if (type == chat::PacketType::S2C_RoomMessage)
                {
                    std::string sender;
                    std::string message;
                    if (reader.ReadString(sender) && reader.ReadString(message))
                    {
                        std::cout << "[" << sender << "] " << message << std::endl;
                    }
                }
                else
                {
                    std::string message;
                    if (reader.ReadString(message))
                    {
                        std::cout << chat::PacketTypeToString(type) << ": " << message << std::endl;
                    }
                }
            }
        }

        std::cout << "Disconnected from server." << std::endl;
    });

    std::string line;
    while (running.load() && std::getline(std::cin, line))
    {
        if (line.empty())
        {
            continue;
        }

        std::vector<char> packet;
        if (line.rfind("/nick ", 0) == 0)
        {
            packet = chat::MakeStringPacket(chat::PacketType::C2S_SetNickname, line.substr(6));
        }
        else if (line.rfind("/create ", 0) == 0)
        {
            packet = chat::MakeStringPacket(chat::PacketType::C2S_CreateRoom, line.substr(8));
        }
        else if (line.rfind("/join ", 0) == 0)
        {
            packet = chat::MakeStringPacket(chat::PacketType::C2S_JoinRoom, line.substr(6));
        }
        else if (line == "/leave")
        {
            chat::BufferWriter writer(chat::PacketType::C2S_LeaveRoom);
            packet = writer.Finalize();
        }
        else if (line == "/quit")
        {
            running = false;
            shutdown(socketHandle, SD_BOTH);
            break;
        }
        else if (line == "/help")
        {
            PrintHelp();
            continue;
        }
        else
        {
            packet = chat::MakeStringPacket(chat::PacketType::C2S_Chat, line);
        }

        if (!SendPacket(socketHandle, packet))
        {
            std::cerr << "send failed" << std::endl;
            running = false;
            break;
        }
    }

    running = false;
    shutdown(socketHandle, SD_BOTH);
    closesocket(socketHandle);

    if (receiver.joinable())
    {
        receiver.join();
    }

    WSACleanup();
    return 0;
}
