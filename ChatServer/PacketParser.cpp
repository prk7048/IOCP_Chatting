#include "PacketParser.h"

#include "../Common/Protocol.h"

bool PacketParser::Append(const char* data, std::size_t length, std::vector<std::vector<char>>& outPackets, std::string& outError)
{
    buffer_.insert(buffer_.end(), data, data + length);

    while (buffer_.size() >= chat::kHeaderSize)
    {
        chat::PacketHeader header {};
        if (!chat::ReadHeader(buffer_.data(), buffer_.size(), header))
        {
            outError = "failed to read packet header";
            return false;
        }

        if (header.size < chat::kHeaderSize || header.size > chat::kMaxPacketSize)
        {
            outError = "invalid packet size";
            return false;
        }

        if (buffer_.size() < header.size)
        {
            break;
        }

        outPackets.emplace_back(buffer_.begin(), buffer_.begin() + header.size);
        buffer_.erase(buffer_.begin(), buffer_.begin() + header.size);
    }

    return true;
}
