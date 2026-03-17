#include "Server.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace
{
constexpr int kPendingAcceptCount = 8;
constexpr int kListenBacklog = SOMAXCONN;

std::vector<char> MakeRoomJoinedPacket(const std::string& roomName)
{
    return chat::MakeStringPacket(chat::PacketType::S2C_RoomJoined, roomName);
}

std::vector<char> MakeRoomLeftPacket(const std::string& roomName)
{
    return chat::MakeStringPacket(chat::PacketType::S2C_RoomLeft, roomName);
}
} // namespace

AcceptContext::AcceptContext()
    : IocpContext(IoOperation::Accept)
{
}

AcceptContext::~AcceptContext()
{
    if (acceptSocket != INVALID_SOCKET)
    {
        closesocket(acceptSocket);
        acceptSocket = INVALID_SOCKET;
    }
}

Server::Server() = default;

Server::~Server()
{
    Stop();
}

bool Server::Start(std::uint16_t port)
{
    if (running_.load())
    {
        return true;
    }

    if (!InitializeWinsock() || !CreateCompletionPort() || !CreateListenSocket(port) || !LoadExtensionFunctions())
    {
        Stop();
        return false;
    }

    running_ = true;

    const unsigned int workerCount = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned int index = 0; index < workerCount; ++index)
    {
        workers_.emplace_back(&Server::WorkerLoop, this);
    }

    for (int index = 0; index < kPendingAcceptCount; ++index)
    {
        if (!PostAccept())
        {
            Stop();
            return false;
        }
    }

    Log("server started on port " + std::to_string(port));
    return true;
}

void Server::Stop()
{
    const bool wasRunning = running_.exchange(false);
    if (!wasRunning && completionPort_ == nullptr && listenSocket_ == INVALID_SOCKET && !winsockInitialized_)
    {
        return;
    }

    if (listenSocket_ != INVALID_SOCKET)
    {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    std::vector<std::shared_ptr<Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (const auto& [id, session] : sessions_)
        {
            sessions.push_back(session);
        }
    }

    for (const auto& session : sessions)
    {
        session->Close("server stopping");
    }

    if (completionPort_ != nullptr)
    {
        for (std::size_t index = 0; index < workers_.size(); ++index)
        {
            PostQueuedCompletionStatus(completionPort_, 0, 0, nullptr);
        }
    }

    for (auto& worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    workers_.clear();

    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.clear();
    }

    {
        std::lock_guard<std::mutex> lock(roomsMutex_);
        rooms_.clear();
    }

    if (completionPort_ != nullptr)
    {
        CloseHandle(completionPort_);
        completionPort_ = nullptr;
    }

    if (winsockInitialized_)
    {
        WSACleanup();
        winsockInitialized_ = false;
    }
}

void Server::DispatchPacket(const std::shared_ptr<Session>& session, const std::vector<char>& packet)
{
    chat::PacketHeader header {};
    if (!chat::ReadHeader(packet.data(), packet.size(), header))
    {
        SendError(session, "header parse failed");
        return;
    }

    const auto packetType = static_cast<chat::PacketType>(header.type);
    chat::BufferReader reader(packet.data() + chat::kHeaderSize, packet.size() - chat::kHeaderSize);

    bool payloadParsed = false;

    switch (packetType)
    {
    case chat::PacketType::C2S_SetNickname:
        payloadParsed = HandleSetNickname(session, reader);
        break;
    case chat::PacketType::C2S_CreateRoom:
        payloadParsed = HandleCreateRoom(session, reader);
        break;
    case chat::PacketType::C2S_JoinRoom:
        payloadParsed = HandleJoinRoom(session, reader);
        break;
    case chat::PacketType::C2S_LeaveRoom:
        payloadParsed = HandleLeaveRoom(session);
        break;
    case chat::PacketType::C2S_Chat:
        payloadParsed = HandleChat(session, reader);
        break;
    default:
        SendError(session, "unknown packet type");
        return;
    }

    if (payloadParsed && !reader.IsFullyConsumed())
    {
        SendError(session, "packet payload has trailing data");
    }
}

void Server::HandleSessionClosed(std::uint64_t sessionId)
{
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        const auto it = sessions_.find(sessionId);
        if (it == sessions_.end())
        {
            return;
        }

        session = it->second;
        sessions_.erase(it);
    }

    LeaveCurrentRoom(session, false);
}

void Server::SendWelcome(const std::shared_ptr<Session>& session)
{
    const std::string nickname = session->GetNickname();
    session->SendPacket(chat::MakeStringPacket(chat::PacketType::S2C_Welcome, "Connected. Use /nick, /create, /join, /leave and chat."));
    session->SendPacket(chat::MakeStringPacket(chat::PacketType::S2C_LoginAck, nickname));

    Log("session#" + std::to_string(session->GetId()) + " connected from " + session->GetRemoteAddress() +
        " as " + nickname);
}

void Server::Log(const std::string& message)
{
    logger_.Info(message);
}

bool Server::InitializeWinsock()
{
    WSADATA wsaData {};
    winsockInitialized_ = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    return winsockInitialized_;
}

bool Server::CreateCompletionPort()
{
    completionPort_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    return completionPort_ != nullptr;
}

bool Server::CreateListenSocket(std::uint16_t port)
{
    listenSocket_ = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (listenSocket_ == INVALID_SOCKET)
    {
        return false;
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        return false;
    }

    if (listen(listenSocket_, kListenBacklog) == SOCKET_ERROR)
    {
        return false;
    }

    return CreateIoCompletionPort(reinterpret_cast<HANDLE>(listenSocket_), completionPort_, 0, 0) != nullptr;
}

bool Server::LoadExtensionFunctions()
{
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes = 0;
    return WSAIoctl(
               listenSocket_,
               SIO_GET_EXTENSION_FUNCTION_POINTER,
               &guidAcceptEx,
               sizeof(guidAcceptEx),
               &acceptEx_,
               sizeof(acceptEx_),
               &bytes,
               nullptr,
               nullptr)
        == 0;
}

bool Server::PostAccept()
{
    if (!running_.load())
    {
        return false;
    }

    auto* context = new AcceptContext();
    context->acceptSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (context->acceptSocket == INVALID_SOCKET)
    {
        delete context;
        return false;
    }

    DWORD receivedBytes = 0;
    const BOOL ok = acceptEx_(
        listenSocket_,
        context->acceptSocket,
        context->buffer.data(),
        0,
        sizeof(sockaddr_in) + 16,
        sizeof(sockaddr_in) + 16,
        &receivedBytes,
        &context->overlapped);

    if (!ok)
    {
        const int error = WSAGetLastError();
        if (error != ERROR_IO_PENDING)
        {
            delete context;
            Log("AcceptEx failed: " + std::to_string(error));
            return false;
        }
    }

    return true;
}

void Server::WorkerLoop()
{
    while (true)
    {
        DWORD transferredBytes = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* overlapped = nullptr;

        const BOOL ok = GetQueuedCompletionStatus(completionPort_, &transferredBytes, &completionKey, &overlapped, INFINITE);
        const DWORD errorCode = ok ? ERROR_SUCCESS : GetLastError();

        if (overlapped == nullptr)
        {
            if (!running_.load())
            {
                break;
            }
            continue;
        }

        auto* context = reinterpret_cast<IocpContext*>(overlapped);
        switch (context->operation)
        {
        case IoOperation::Accept:
        {
            auto* acceptContext = static_cast<AcceptContext*>(context);
            HandleAcceptedClient(acceptContext, errorCode);
            delete acceptContext;
            break;
        }
        case IoOperation::Recv:
        case IoOperation::Send:
        {
            auto* sessionContext = static_cast<SessionIoContext*>(context);
            sessionContext->owner->OnIoCompleted(sessionContext, transferredBytes, errorCode);
            delete sessionContext;
            break;
        }
        default:
            delete context;
            break;
        }
    }
}

void Server::HandleAcceptedClient(AcceptContext* context, DWORD errorCode)
{
    if (running_.load())
    {
        PostAccept();
    }

    if (errorCode != ERROR_SUCCESS)
    {
        Log("accept completion failed: " + std::to_string(errorCode));
        return;
    }

    if (setsockopt(context->acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<const char*>(&listenSocket_), sizeof(listenSocket_))
        == SOCKET_ERROR)
    {
        Log("SO_UPDATE_ACCEPT_CONTEXT failed: " + std::to_string(WSAGetLastError()));
        return;
    }

    const BOOL nodelay = TRUE;
    setsockopt(context->acceptSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(context->acceptSocket), completionPort_, 0, 0) == nullptr)
    {
        Log("CreateIoCompletionPort(client) failed: " + std::to_string(GetLastError()));
        return;
    }

    const std::uint64_t sessionId = nextSessionId_.fetch_add(1);
    auto session = std::make_shared<Session>(*this, sessionId, context->acceptSocket, SocketAddressToString(context->acceptSocket));
    context->acceptSocket = INVALID_SOCKET;

    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_[sessionId] = session;
    }

    session->Start();
}

std::shared_ptr<Room> Server::FindRoom(const std::string& roomName)
{
    std::lock_guard<std::mutex> lock(roomsMutex_);
    const auto it = rooms_.find(roomName);
    return it == rooms_.end() ? nullptr : it->second;
}

std::shared_ptr<Room> Server::CreateRoom(const std::string& roomName)
{
    std::lock_guard<std::mutex> lock(roomsMutex_);
    const auto it = rooms_.find(roomName);
    if (it != rooms_.end())
    {
        return nullptr;
    }

    auto room = std::make_shared<Room>(roomName);
    rooms_[roomName] = room;
    return room;
}

void Server::RemoveEmptyRoom(const std::string& roomName)
{
    std::lock_guard<std::mutex> lock(roomsMutex_);
    const auto it = rooms_.find(roomName);
    if (it != rooms_.end() && it->second->GetMemberCount() == 0)
    {
        rooms_.erase(it);
        Log("room removed: " + roomName);
    }
}

bool Server::HandleSetNickname(const std::shared_ptr<Session>& session, chat::BufferReader& reader)
{
    std::string nickname;
    if (!reader.ReadString(nickname) || !IsValidName(nickname))
    {
        SendError(session, "nickname must be 1-20 chars and contain no spaces");
        return false;
    }

    session->SetNickname(nickname);
    session->SendPacket(chat::MakeStringPacket(chat::PacketType::S2C_LoginAck, nickname));
    SendSystemMessage(session, "nickname set to " + nickname);

    Log("session#" + std::to_string(session->GetId()) + " nickname => " + nickname);
    return true;
}

bool Server::HandleCreateRoom(const std::shared_ptr<Session>& session, chat::BufferReader& reader)
{
    std::string roomName;
    if (!reader.ReadString(roomName) || !IsValidName(roomName))
    {
        SendError(session, "room name must be 1-20 chars and contain no spaces");
        return false;
    }

    auto room = CreateRoom(roomName);
    if (!room)
    {
        SendError(session, "room already exists");
        return true;
    }

    Log("room created: " + roomName + " by " + session->GetNickname());
    JoinRoom(session, room, true);
    return true;
}

bool Server::HandleJoinRoom(const std::shared_ptr<Session>& session, chat::BufferReader& reader)
{
    std::string roomName;
    if (!reader.ReadString(roomName) || !IsValidName(roomName))
    {
        SendError(session, "room name must be 1-20 chars and contain no spaces");
        return false;
    }

    auto room = FindRoom(roomName);
    if (!room)
    {
        SendError(session, "room does not exist");
        return true;
    }

    JoinRoom(session, room, false);
    return true;
}

bool Server::HandleLeaveRoom(const std::shared_ptr<Session>& session)
{
    if (session->GetCurrentRoom().empty())
    {
        SendError(session, "not in a room");
        return true;
    }

    LeaveCurrentRoom(session, true);
    return true;
}

bool Server::HandleChat(const std::shared_ptr<Session>& session, chat::BufferReader& reader)
{
    std::string message;
    if (!reader.ReadString(message) || message.empty())
    {
        SendError(session, "message cannot be empty");
        return false;
    }

    const std::string roomName = session->GetCurrentRoom();
    if (roomName.empty())
    {
        SendError(session, "join a room first");
        return true;
    }

    auto room = FindRoom(roomName);
    if (!room)
    {
        session->ClearCurrentRoom();
        SendError(session, "room no longer exists");
        return true;
    }

    BroadcastChatToRoom(room, session->GetNickname(), message);
    return true;
}

bool Server::JoinRoom(const std::shared_ptr<Session>& session, const std::shared_ptr<Room>& room, bool createdByUser)
{
    const std::string previousRoom = session->GetCurrentRoom();
    if (previousRoom == room->GetName())
    {
        SendSystemMessage(session, "already in room " + previousRoom);
        return true;
    }

    LeaveCurrentRoom(session, false);

    room->AddSession(session);
    session->SetCurrentRoom(room->GetName());
    session->SendPacket(MakeRoomJoinedPacket(room->GetName()));

    if (createdByUser)
    {
        SendSystemMessage(session, "room created: " + room->GetName());
    }
    else
    {
        SendSystemMessage(session, "joined room: " + room->GetName());
    }

    BroadcastSystemToRoom(room, session->GetNickname() + " entered the room.");
    Log("session#" + std::to_string(session->GetId()) + " joined room " + room->GetName());
    return true;
}

void Server::LeaveCurrentRoom(const std::shared_ptr<Session>& session, bool notifySelf)
{
    const std::string roomName = session->GetCurrentRoom();
    if (roomName.empty())
    {
        return;
    }

    auto room = FindRoom(roomName);
    session->ClearCurrentRoom();

    if (!room)
    {
        return;
    }

    room->RemoveSession(session->GetId());

    if (notifySelf)
    {
        session->SendPacket(MakeRoomLeftPacket(roomName));
        SendSystemMessage(session, "left room: " + roomName);
    }

    BroadcastSystemToRoom(room, session->GetNickname() + " left the room.");
    Log("session#" + std::to_string(session->GetId()) + " left room " + roomName);
    RemoveEmptyRoom(roomName);
}

void Server::BroadcastSystemToRoom(const std::shared_ptr<Room>& room, const std::string& message)
{
    const auto packet = chat::MakeStringPacket(chat::PacketType::S2C_SystemMessage, message);
    const auto members = room->GetMembersSnapshot();
    for (const auto& member : members)
    {
        member->SendPacket(packet);
    }
}

void Server::BroadcastChatToRoom(const std::shared_ptr<Room>& room, const std::string& sender, const std::string& message)
{
    const auto packet = chat::MakeTwoStringPacket(chat::PacketType::S2C_RoomMessage, sender, message);
    const auto members = room->GetMembersSnapshot();
    for (const auto& member : members)
    {
        member->SendPacket(packet);
    }

    Log("[" + room->GetName() + "] " + sender + ": " + message);
}

void Server::SendSystemMessage(const std::shared_ptr<Session>& session, const std::string& message)
{
    session->SendPacket(chat::MakeStringPacket(chat::PacketType::S2C_SystemMessage, message));
}

void Server::SendError(const std::shared_ptr<Session>& session, const std::string& message)
{
    session->SendPacket(chat::MakeStringPacket(chat::PacketType::S2C_Error, message));
}

bool Server::IsValidName(const std::string& value)
{
    if (value.empty() || value.size() > 20)
    {
        return false;
    }

    return std::none_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

std::string Server::SocketAddressToString(SOCKET socket)
{
    sockaddr_storage address {};
    int addressLength = sizeof(address);
    if (getpeername(socket, reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCKET_ERROR)
    {
        return "unknown";
    }

    char ipBuffer[INET6_ADDRSTRLEN] = {};
    unsigned short port = 0;

    if (address.ss_family == AF_INET)
    {
        auto* ipv4 = reinterpret_cast<sockaddr_in*>(&address);
        InetNtopA(AF_INET, &ipv4->sin_addr, ipBuffer, sizeof(ipBuffer));
        port = ntohs(ipv4->sin_port);
    }
    else if (address.ss_family == AF_INET6)
    {
        auto* ipv6 = reinterpret_cast<sockaddr_in6*>(&address);
        InetNtopA(AF_INET6, &ipv6->sin6_addr, ipBuffer, sizeof(ipBuffer));
        port = ntohs(ipv6->sin6_port);
    }

    return std::string(ipBuffer) + ":" + std::to_string(port);
}
