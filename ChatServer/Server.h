#pragma once

#include <WinSock2.h>
#include <MSWSock.h>
#include <WS2tcpip.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../Common/Protocol.h"
#include "Logger.h"
#include "Room.h"
#include "Session.h"

struct AcceptContext final : IocpContext
{
    AcceptContext();
    ~AcceptContext();

    SOCKET acceptSocket = INVALID_SOCKET;
    std::array<char, (sizeof(sockaddr_in) + 16) * 2> buffer {};
};

class Server
{
public:
    Server();
    ~Server();

    bool Start(std::uint16_t port);
    void Stop();

    void DispatchPacket(const std::shared_ptr<Session>& session, const std::vector<char>& packet);
    void HandleSessionClosed(std::uint64_t sessionId);
    void SendWelcome(const std::shared_ptr<Session>& session);
    void Log(const std::string& message);

private:
    bool InitializeWinsock();
    bool CreateCompletionPort();
    bool CreateListenSocket(std::uint16_t port);
    bool LoadExtensionFunctions();
    bool PostAccept();
    void WorkerLoop();
    void HandleAcceptedClient(AcceptContext* context, DWORD errorCode);

    std::shared_ptr<Room> FindRoom(const std::string& roomName);
    std::shared_ptr<Room> CreateRoom(const std::string& roomName);
    void RemoveEmptyRoom(const std::string& roomName);

    bool HandleSetNickname(const std::shared_ptr<Session>& session, chat::BufferReader& reader);
    bool HandleCreateRoom(const std::shared_ptr<Session>& session, chat::BufferReader& reader);
    bool HandleJoinRoom(const std::shared_ptr<Session>& session, chat::BufferReader& reader);
    bool HandleLeaveRoom(const std::shared_ptr<Session>& session);
    bool HandleChat(const std::shared_ptr<Session>& session, chat::BufferReader& reader);

    bool JoinRoom(const std::shared_ptr<Session>& session, const std::shared_ptr<Room>& room, bool createdByUser);
    void LeaveCurrentRoom(const std::shared_ptr<Session>& session, bool notifySelf);
    void BroadcastSystemToRoom(const std::shared_ptr<Room>& room, const std::string& message);
    void BroadcastChatToRoom(const std::shared_ptr<Room>& room, const std::string& sender, const std::string& message);
    void SendSystemMessage(const std::shared_ptr<Session>& session, const std::string& message);
    void SendError(const std::shared_ptr<Session>& session, const std::string& message);

    static bool IsValidName(const std::string& value);
    static std::string SocketAddressToString(SOCKET socket);

    HANDLE completionPort_ = nullptr;
    SOCKET listenSocket_ = INVALID_SOCKET;
    LPFN_ACCEPTEX acceptEx_ = nullptr;
    std::atomic<bool> running_ = false;
    bool winsockInitialized_ = false;
    std::vector<std::thread> workers_;
    Logger logger_;

    std::mutex sessionsMutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Session>> sessions_;

    std::mutex roomsMutex_;
    std::unordered_map<std::string, std::shared_ptr<Room>> rooms_;

    std::atomic<std::uint64_t> nextSessionId_ = 1;
};
