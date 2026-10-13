// Clean-room reconstruction — logging.
#pragma once
#include <cstdio>
#include <string>

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
// Dev tools that react to log lines (WFC_CPUPROF's hitch capture on "SLOWFRAME" lines): set once at startup, called with each
// line (first ~150 chars) on the logging thread; null = nothing.
void setLogLineHook(void (*hook)(const char* line));
// Where the per-run user files go (wfc.log, wfc_profile.ini, wfc_characters.ini, crash / hang reports): the working
// directory when it is writable - harnesses start one exe from per-run folders and collect them there - else the exe's
// folder (a launch whose start folder is not writable, e.g. C:\Windows\System32, used to lose the log and the settings
// silently). Decided once, at the first call (the log's open); userDirPrefix() is "" (the working directory) or
// "<exe dir>/", never allocates after that first call (crash filters use it).
const char* userDirPrefix();
inline std::string userFilePath(const char* name) { return std::string(userDirPrefix()) + name; }

} // namespace core

#define LOG_INFO(...)  ::core::logMessage(::core::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::core::logMessage(::core::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::core::logMessage(::core::LogLevel::Error, __VA_ARGS__)
#define LOG_TRACE(...) ::core::logMessage(::core::LogLevel::Trace, __VA_ARGS__)
