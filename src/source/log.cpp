#include "log.h"

#include <windows.h>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace
{
    std::mutex p_mutex;
    std::vector<std::string> p_lines;
    std::uint64_t p_revision = 0;
    bool p_fileInitialized = false;
    constexpr const char* p_arrowToken = "/arrow";
    constexpr const char* p_arrowPrefix = "\t\xE2\x86\xB3 ";

    void writeFile(const char* message)
    {
        wchar_t executable[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0)
        {
            return;
        }

        const std::filesystem::path path = std::filesystem::path(executable).parent_path() / "sampo.log";
        std::ios::openmode mode = std::ios::binary;
        if (p_fileInitialized)
        {
            mode |= std::ios::app;
        }
        else
        {
            mode |= std::ios::trunc;
        }
        std::ofstream file(path, mode);
        if (!file)
        {
            return;
        }

        file << message << '\n';
        p_fileInitialized = true;
    }

    std::string trim(const std::string& value)
    {
        std::size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0)
        {
            ++first;
        }

        std::size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0)
        {
            --last;
        }

        return value.substr(first, last - first);
    }

    void storeLine(const std::string& line)
    {
        OutputDebugStringA(line.c_str());
        OutputDebugStringA("\n");
        p_lines.emplace_back(line);
        writeFile(line.c_str());
    }
}

void sampo::log::write(const char* format, ...)
{
    char message[4096]{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    std::scoped_lock lock(p_mutex);
    const std::string formattedMessage(message);
    std::size_t arrowPosition = formattedMessage.find(p_arrowToken);
    std::string firstLine = trim(formattedMessage.substr(0, arrowPosition));
    if (!firstLine.starts_with("[SAMPO]"))
    {
        firstLine.insert(0, "[SAMPO] ");
    }
    storeLine(firstLine);

    while (arrowPosition != std::string::npos)
    {
        const std::size_t valueStart = arrowPosition + std::strlen(p_arrowToken);
        const std::size_t nextArrowPosition = formattedMessage.find(p_arrowToken, valueStart);
        std::string arrowValue;
        if (nextArrowPosition == std::string::npos)
        {
            arrowValue = trim(formattedMessage.substr(valueStart));
        }
        else
        {
            const std::size_t arrowValueLength = nextArrowPosition - valueStart;
            arrowValue = trim(formattedMessage.substr(valueStart, arrowValueLength));
        }
        if (!arrowValue.empty())
        {
            arrowValue.insert(0, p_arrowPrefix);
            storeLine(arrowValue);
        }
        arrowPosition = nextArrowPosition;
    }

    ++p_revision;
}

std::vector<std::string> sampo::log::snapshot()
{
    std::scoped_lock lock(p_mutex);
    return p_lines;
}

std::string sampo::log::text()
{
    std::scoped_lock lock(p_mutex);
    std::string output;
    for (const std::string& line : p_lines)
    {
        output.append(line).push_back('\n');
    }
    return output;
}

std::uint64_t sampo::log::revision()
{
    std::scoped_lock lock(p_mutex);
    return p_revision;
}
