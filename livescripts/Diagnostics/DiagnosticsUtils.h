#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>

namespace Diagnostics
{
    inline uint64_t UnixMilliseconds()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    inline std::string CleanField(std::string value)
    {
        for (char& ch : value)
        {
            if (ch == '\t' || ch == '\r' || ch == '\n')
                ch = ' ';
        }
        return value;
    }

    inline void WriteCleanField(std::ostream& os, std::string_view value)
    {
        for (char ch : value)
        {
            if (ch == '\t' || ch == '\r' || ch == '\n')
                os.put(' ');
            else
                os.put(ch);
        }
    }

    struct CleanFieldWriter
    {
        std::string_view value;
        friend std::ostream& operator<<(std::ostream& os, const CleanFieldWriter& writer)
        {
            WriteCleanField(os, writer.value);
            return os;
        }
    };

    inline CleanFieldWriter AsCleanField(std::string_view value)
    {
        return CleanFieldWriter{ value };
    }

    inline std::string NormalizeName(std::string value)
    {
        value.erase(value.begin(), std::find_if(value.begin(), value.end(),
            [](unsigned char ch) { return !std::isspace(ch); }));
        value.erase(std::find_if(value.rbegin(), value.rend(),
            [](unsigned char ch) { return !std::isspace(ch); }).base(), value.end());
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    inline bool EnsureParentDirectory(const std::string& path)
    {
        std::error_code error;
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, error);
        return !error;
    }

    inline bool IsEmptyOrMissing(const std::string& path)
    {
        std::error_code error;
        return !std::filesystem::exists(path, error) ||
            std::filesystem::file_size(path, error) == 0;
    }

    inline std::string FormatTimestampIso(uint64_t unixMs = 0)
    {
        if (unixMs == 0)
            unixMs = UnixMilliseconds();
        std::time_t t = static_cast<std::time_t>(unixMs / 1000);
        uint32_t ms = static_cast<uint32_t>(unixMs % 1000);
        std::tm tmBuf{};
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tmBuf, &t);
#else
        localtime_r(&t, &tmBuf);
#endif
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03u",
            tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
            tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, ms);
        return std::string(buf);
    }
}
