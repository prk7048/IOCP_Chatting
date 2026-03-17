#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

class Logger
{
public:
    void Info(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << Timestamp() << " " << message << std::endl;
    }

private:
    static std::string Timestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm localTime {};
        localtime_s(&localTime, &nowTime);

        std::ostringstream stream;
        stream << "[" << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << "]";
        return stream.str();
    }

    std::mutex mutex_;
};
