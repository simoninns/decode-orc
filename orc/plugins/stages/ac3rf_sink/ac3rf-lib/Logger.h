// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#ifndef MUSECPP_LOGGER_H
#define MUSECPP_LOGGER_H

#include <string>

enum LogPriority {
    eDebug = 1,
    eInfo = 2,
    eWarn = 3,
    eError = 4,
    eOff = 100,
};

enum LogCategoryFlags {
    eApplication = 1,
    ePerformance = 2,
    eAudio = 4,
    eVideo = 8,
    eDecoder = 16,
    eInput = 32,
    eOutput = 64,
};

inline LogCategoryFlags operator|(LogCategoryFlags a, LogCategoryFlags b)
{
    return static_cast<LogCategoryFlags>(static_cast<int>(a) | static_cast<int>(b));
}

class Logger {
public:
    virtual ~Logger() = default;

    virtual void log(LogPriority priority, LogCategoryFlags categorization, const std::string &message) = 0;
    [[nodiscard]] virtual bool isEnabled(LogPriority priority, LogCategoryFlags categorization) const = 0;
    virtual void sync() = 0;

    void error(LogCategoryFlags c, const std::string &m) { log(eError, c, m); }
    void warn (LogCategoryFlags c, const std::string &m) { log(eWarn,  c, m); }
    void info (LogCategoryFlags c, const std::string &m) { log(eInfo,  c, m); }
    void debug(LogCategoryFlags c, const std::string &m) { log(eDebug, c, m); }
};

#endif //MUSECPP_LOGGER_H
