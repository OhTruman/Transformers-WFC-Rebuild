// Clean-room reconstruction — logging.
#pragma once
#include <cstdio>

namespace core {

enum class LogLevel { Trace, Info, Warn, Error };

void logMessage(LogLevel level, const char* fmt, ...);
// Lines are written by a background thread (see Log.cpp); this writes everything logged so far, in order, before returning
// (the crash handler, shutdown paths). Errors are flushed this way automatically.
void logFlush();

} // namespace core

#define LOG_INFO(...)  ::core::logMessage(::core::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::core::logMessage(::core::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::core::logMessage(::core::LogLevel::Error, __VA_ARGS__)
#define LOG_TRACE(...) ::core::logMessage(::core::LogLevel::Trace, __VA_ARGS__)
