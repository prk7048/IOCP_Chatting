#include "Session.h"

#include <utility>

#include "../Common/Protocol.h"
#include "Server.h"

SessionIoContext::SessionIoContext(IoOperation operation, std::shared_ptr<Session> owner)
    : IocpContext(operation)
    , owner(std::move(owner))
{
    wsabuf.buf = buffer.data();
    wsabuf.len = static_cast<ULONG>(buffer.size());
}

Session::Session(Server& server, std::uint64_t id, SOCKET socket, std::string remoteAddress)
    : server_(server)
    , id_(id)
    , socket_(socket)
    , remoteAddress_(std::move(remoteAddress))
    , nickname_("Guest" + std::to_string(id))
{
}

Session::~Session()
{
    if (socket_ != INVALID_SOCKET)
    {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

void Session::Start()
{
    server_.SendWelcome(shared_from_this());
    PostRecv();
}

void Session::OnIoCompleted(SessionIoContext* context, DWORD transferredBytes, DWORD errorCode)
{
    switch (context->operation)
    {
    case IoOperation::Recv:
        HandleRecv(context, transferredBytes, errorCode);
        break;
    case IoOperation::Send:
        HandleSend(context, errorCode);
        break;
    default:
        break;
    }
}

void Session::SendPacket(std::vector<char> packet)
{
    if (closing_.load())
    {
        return;
    }

    bool shouldStart = false;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendQueue_.push_back(std::move(packet));
        if (!sendInProgress_)
        {
            sendInProgress_ = true;
            shouldStart = true;
        }
    }

    if (shouldStart)
    {
        StartNextSend();
    }
}

void Session::Close(const std::string& reason)
{
    if (closing_.exchange(true))
    {
        return;
    }

    if (socket_ != INVALID_SOCKET)
    {
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    server_.Log("session#" + std::to_string(id_) + " closed (" + remoteAddress_ + "): " + reason);
    server_.HandleSessionClosed(id_);
}

std::uint64_t Session::GetId() const
{
    return id_;
}

std::string Session::GetNickname() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return nickname_;
}

void Session::SetNickname(const std::string& nickname)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    nickname_ = nickname;
}

std::string Session::GetCurrentRoom() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentRoom_;
}

void Session::SetCurrentRoom(const std::string& roomName)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    currentRoom_ = roomName;
}

void Session::ClearCurrentRoom()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    currentRoom_.clear();
}

std::string Session::GetRemoteAddress() const
{
    return remoteAddress_;
}

bool Session::IsClosing() const
{
    return closing_.load();
}

void Session::PostRecv()
{
    if (closing_.load())
    {
        return;
    }

    auto* context = new SessionIoContext(IoOperation::Recv, shared_from_this());
    DWORD receivedBytes = 0;
    const int result = WSARecv(socket_, &context->wsabuf, 1, &receivedBytes, &context->flags, &context->overlapped, nullptr);
    if (result == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            delete context;
            Close("WSARecv failed: " + std::to_string(error));
        }
    }
}

void Session::HandleRecv(SessionIoContext* context, DWORD transferredBytes, DWORD errorCode)
{
    if (errorCode != ERROR_SUCCESS)
    {
        Close("recv completion failed: " + std::to_string(errorCode));
        return;
    }

    if (transferredBytes == 0)
    {
        Close("peer disconnected");
        return;
    }

    std::vector<std::vector<char>> packets;
    std::string parseError;
    {
        std::lock_guard<std::mutex> lock(parserMutex_);
        if (!parser_.Append(context->buffer.data(), transferredBytes, packets, parseError))
        {
            Close("packet parse error: " + parseError);
            return;
        }
    }

    for (const auto& packet : packets)
    {
        server_.DispatchPacket(shared_from_this(), packet);
    }

    PostRecv();
}

void Session::HandleSend(SessionIoContext* /*context*/, DWORD errorCode)
{
    if (errorCode != ERROR_SUCCESS)
    {
        Close("send completion failed: " + std::to_string(errorCode));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (!sendQueue_.empty())
        {
            sendQueue_.pop_front();
        }
    }

    StartNextSend();
}

void Session::StartNextSend()
{
    if (closing_.load())
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendQueue_.clear();
        sendInProgress_ = false;
        return;
    }

    std::vector<char> packet;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (sendQueue_.empty())
        {
            sendInProgress_ = false;
            return;
        }

        packet = sendQueue_.front();
    }

    auto* context = new SessionIoContext(IoOperation::Send, shared_from_this());
    context->payload = std::move(packet);
    context->wsabuf.buf = context->payload.data();
    context->wsabuf.len = static_cast<ULONG>(context->payload.size());

    DWORD sentBytes = 0;
    const int result = WSASend(socket_, &context->wsabuf, 1, &sentBytes, 0, &context->overlapped, nullptr);
    if (result == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            delete context;
            Close("WSASend failed: " + std::to_string(error));
        }
    }
}
