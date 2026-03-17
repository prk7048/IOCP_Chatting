#pragma once

#include <WinSock2.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "PacketParser.h"

class Server;

enum class IoOperation
{
    Accept,
    Recv,
    Send
};

struct IocpContext
{
    explicit IocpContext(IoOperation operation)
        : operation(operation)
    {
    }

    OVERLAPPED overlapped {};
    IoOperation operation;
};

class Session;

struct SessionIoContext final : IocpContext
{
    SessionIoContext(IoOperation operation, std::shared_ptr<Session> owner);

    std::shared_ptr<Session> owner;
    WSABUF wsabuf {};
    std::array<char, 8192> buffer {};
    std::vector<char> payload;
    DWORD flags = 0;
};

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(Server& server, std::uint64_t id, SOCKET socket, std::string remoteAddress);
    ~Session();

    void Start();
    void OnIoCompleted(SessionIoContext* context, DWORD transferredBytes, DWORD errorCode);
    void SendPacket(std::vector<char> packet);
    void Close(const std::string& reason);

    std::uint64_t GetId() const;
    std::string GetNickname() const;
    void SetNickname(const std::string& nickname);
    std::string GetCurrentRoom() const;
    void SetCurrentRoom(const std::string& roomName);
    void ClearCurrentRoom();
    std::string GetRemoteAddress() const;
    bool IsClosing() const;

private:
    void PostRecv();
    void HandleRecv(SessionIoContext* context, DWORD transferredBytes, DWORD errorCode);
    void HandleSend(SessionIoContext* context, DWORD errorCode);
    void StartNextSend();

    Server& server_;
    const std::uint64_t id_;
    SOCKET socket_;
    const std::string remoteAddress_;

    mutable std::mutex stateMutex_;
    std::string nickname_;
    std::string currentRoom_;

    std::mutex parserMutex_;
    PacketParser parser_;

    std::mutex sendMutex_;
    std::deque<std::vector<char>> sendQueue_;
    bool sendInProgress_ = false;

    std::atomic<bool> closing_ = false;
};
