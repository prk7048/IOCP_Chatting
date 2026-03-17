#pragma once

#include <winsock2.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace chat
{
constexpr std::size_t kHeaderSize = sizeof(std::uint16_t) * 2;
constexpr std::size_t kMaxPacketSize = 4096;
constexpr std::size_t kMaxStringFieldSize = 512;

enum class PacketType : std::uint16_t
{
    C2S_SetNickname = 1,
    C2S_CreateRoom = 2,
    C2S_JoinRoom = 3,
    C2S_LeaveRoom = 4,
    C2S_Chat = 5,

    S2C_Welcome = 100,
    S2C_LoginAck = 101,
    S2C_RoomJoined = 102,
    S2C_RoomLeft = 103,
    S2C_RoomMessage = 104,
    S2C_SystemMessage = 105,
    S2C_Error = 106
};

struct PacketHeader
{
    std::uint16_t size = 0;
    std::uint16_t type = 0;
};

inline bool ReadHeader(const char* data, std::size_t dataSize, PacketHeader& outHeader)
{
    if (dataSize < kHeaderSize)
    {
        return false;
    }

    PacketHeader networkHeader {};
    std::memcpy(&networkHeader, data, kHeaderSize);
    outHeader.size = ntohs(networkHeader.size);
    outHeader.type = ntohs(networkHeader.type);
    return true;
}

class BufferWriter
{
public:
    explicit BufferWriter(PacketType type)
        : type_(type)
    {
        buffer_.resize(kHeaderSize);
    }

    void WriteUInt16(std::uint16_t value)
    {
        const std::uint16_t networkValue = htons(value);
        const char* bytes = reinterpret_cast<const char*>(&networkValue);
        buffer_.insert(buffer_.end(), bytes, bytes + sizeof(networkValue));
    }

    void WriteString(std::string_view value)
    {
        if (value.size() > kMaxStringFieldSize)
        {
            throw std::length_error("string field is too long");
        }

        WriteUInt16(static_cast<std::uint16_t>(value.size()));
        buffer_.insert(buffer_.end(), value.begin(), value.end());
    }

    std::vector<char> Finalize()
    {
        if (buffer_.size() > kMaxPacketSize)
        {
            throw std::length_error("packet is too large");
        }

        PacketHeader header {};
        header.size = htons(static_cast<std::uint16_t>(buffer_.size()));
        header.type = htons(static_cast<std::uint16_t>(type_));
        std::memcpy(buffer_.data(), &header, kHeaderSize);
        return buffer_;
    }

private:
    PacketType type_;
    std::vector<char> buffer_;
};

class BufferReader
{
public:
    BufferReader(const char* data, std::size_t size)
        : data_(data)
        , size_(size)
    {
    }

    bool ReadUInt16(std::uint16_t& value)
    {
        if (offset_ + sizeof(std::uint16_t) > size_)
        {
            return false;
        }

        std::uint16_t networkValue = 0;
        std::memcpy(&networkValue, data_ + offset_, sizeof(networkValue));
        value = ntohs(networkValue);
        offset_ += sizeof(networkValue);
        return true;
    }

    bool ReadString(std::string& value)
    {
        std::uint16_t length = 0;
        if (!ReadUInt16(length))
        {
            return false;
        }

        if (length > kMaxStringFieldSize || offset_ + length > size_)
        {
            return false;
        }

        value.assign(data_ + offset_, data_ + offset_ + length);
        offset_ += length;
        return true;
    }

    bool IsFullyConsumed() const
    {
        return offset_ == size_;
    }

private:
    const char* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
};

inline std::vector<char> MakeStringPacket(PacketType type, std::string_view message)
{
    BufferWriter writer(type);
    writer.WriteString(message);
    return writer.Finalize();
}

inline std::vector<char> MakeTwoStringPacket(PacketType type, std::string_view first, std::string_view second)
{
    BufferWriter writer(type);
    writer.WriteString(first);
    writer.WriteString(second);
    return writer.Finalize();
}

inline const char* PacketTypeToString(PacketType type)
{
    switch (type)
    {
    case PacketType::C2S_SetNickname:
        return "C2S_SetNickname";
    case PacketType::C2S_CreateRoom:
        return "C2S_CreateRoom";
    case PacketType::C2S_JoinRoom:
        return "C2S_JoinRoom";
    case PacketType::C2S_LeaveRoom:
        return "C2S_LeaveRoom";
    case PacketType::C2S_Chat:
        return "C2S_Chat";
    case PacketType::S2C_Welcome:
        return "S2C_Welcome";
    case PacketType::S2C_LoginAck:
        return "S2C_LoginAck";
    case PacketType::S2C_RoomJoined:
        return "S2C_RoomJoined";
    case PacketType::S2C_RoomLeft:
        return "S2C_RoomLeft";
    case PacketType::S2C_RoomMessage:
        return "S2C_RoomMessage";
    case PacketType::S2C_SystemMessage:
        return "S2C_SystemMessage";
    case PacketType::S2C_Error:
        return "S2C_Error";
    default:
        return "Unknown";
    }
}
} // namespace chat
