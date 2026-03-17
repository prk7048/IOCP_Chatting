#include "Room.h"

#include <utility>

#include "Session.h"

Room::Room(std::string name)
    : name_(std::move(name))
{
}

const std::string& Room::GetName() const
{
    return name_;
}

void Room::AddSession(const std::shared_ptr<Session>& session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    members_[session->GetId()] = session;
}

void Room::RemoveSession(std::uint64_t sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    members_.erase(sessionId);
}

std::vector<std::shared_ptr<Session>> Room::GetMembersSnapshot() const
{
    std::vector<std::shared_ptr<Session>> members;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = members_.begin(); it != members_.end();)
    {
        if (const auto session = it->second.lock())
        {
            members.push_back(session);
            ++it;
        }
        else
        {
            it = members_.erase(it);
        }
    }

    return members;
}

std::size_t Room::GetMemberCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return members_.size();
}
