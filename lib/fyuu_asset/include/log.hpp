#pragma once

#define LOG_TRACE(msg) if (log::Trace) { log::Trace(msg, std::source_location::current()); }
#define LOG_DEBUG(msg) if (log::Debug) { log::Debug(msg, std::source_location::current()); }
#define LOG_INFO(msg) if (log::Info) { log::Info(msg, std::source_location::current()); }
#define LOG_WARNING(msg) if (log::Warning) { log::Warning(msg, std::source_location::current()); }
#define LOG_ERROR(msg) if (log::Error) { log::Error(msg, std::source_location::current()); }
#define LOG_FATAL(msg) if (log::Fatal) { log::Fatal(msg, std::source_location::current()); }