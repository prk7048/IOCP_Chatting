#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Session;

class Room
{
public:
    explicit Room(std::string name);

    const std::string& GetName() const;
    void AddSession(const std::shared_ptr<Session>& session);
    void RemoveSession(std::uint64_t sessionId);
    std::vector<std::shared_ptr<Session>> GetMembersSnapshot() const;
    std::size_t GetMemberCount() const;

private:
    std::string name_;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::uint64_t, std::weak_ptr<Session>> members_;
};
