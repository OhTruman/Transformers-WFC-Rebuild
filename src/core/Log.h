// Clean-room reconstruction — logging.
#pragma once
#include <cstdio>

namespace core {

enum class LogLevel { Trace, Info, Warn, Error };

void logMessage(LogLevel level, const char* fmt, ...);
// Lines are written by a background thread (see Log.cpp); this writes everything logged so far, in order, before returning
// (shutdown paths; a crash filter uses logTryFlush below). Errors are flushed this way automatically.
void logFlush();
// Crash-handler variant: writes what is pending only if no thread holds the writer's locks (try_lock), with raw OS writes,
// never blocking or allocating; returns false if it could not (the lines are then lost, the crash report is not).
bool logTryFlush();
// The most recent log line (first ~150 chars; diagnostics only - racy by design, never blocks): dev tools use it as context.
const char* logLastLine();

} // namespace core

#define LOG_INFO(...)  ::core::logMessage(::core::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::core::logMessage(::core::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::core::logMessage(::core::LogLevel::Error, __VA_ARGS__)
#define LOG_TRACE(...) ::core::logMessage(::core::LogLevel::Trace, __VA_ARGS__)
