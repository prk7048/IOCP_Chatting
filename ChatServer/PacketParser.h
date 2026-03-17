#pragma once

#include <string>
#include <vector>

class PacketParser
{
public:
    bool Append(const char* data, std::size_t length, std::vector<std::vector<char>>& outPackets, std::string& outError);

private:
    std::vector<char> buffer_;
};
