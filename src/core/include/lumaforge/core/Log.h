#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace lf::log {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error };

[[nodiscard]] constexpr std::string_view levelName(Level l) noexcept {
    switch (l) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

struct Entry {
    std::chrono::system_clock::time_point time;
    Level level;
    std::uint32_t threadId;
    std::string category;   // "hw", "decode", "render", "export", "ui", ...
    std::string message;
};

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const Entry& entry) = 0;
    virtual void flush() {}
};

// Keeps the last N entries in memory for the Diagnostics window and notifies
// listeners (used by the UI to append live). Listener callbacks run on the
// logging thread; the UI must marshal to its own thread.
class RingBufferSink final : public Sink {
public:
    explicit RingBufferSink(std::size_t capacity = 2000) : m_capacity(capacity) {}
    void write(const Entry& entry) override;
    [[nodiscard]] std::vector<Entry> snapshot() const;
    void setListener(std::function<void(const Entry&)> listener);

private:
    mutable std::mutex m_mutex;
    std::size_t m_capacity;
    std::deque<Entry> m_entries;
    std::function<void(const Entry&)> m_listener;
};

class FileSink final : public Sink {
public:
    explicit FileSink(const std::wstring& path);
    ~FileSink() override;
    void write(const Entry& entry) override;
    void flush() override;
    [[nodiscard]] bool isOpen() const noexcept { return m_file != nullptr; }

private:
    std::mutex m_mutex;
    void* m_file = nullptr;  // FILE*
};

// Writes to the debugger (OutputDebugString) — free when no debugger is attached.
class DebuggerSink final : public Sink {
public:
    void write(const Entry& entry) override;
};

class Logger {
public:
    static Logger& instance();

    void addSink(std::shared_ptr<Sink> sink);
    void setMinimumLevel(Level level) noexcept { m_minLevel = level; }
    [[nodiscard]] Level minimumLevel() const noexcept { return m_minLevel; }
    [[nodiscard]] bool enabled(Level level) const noexcept { return level >= m_minLevel; }

    void log(Level level, std::string_view category, std::string message);
    void flush();

private:
    Logger() = default;
    std::mutex m_sinkMutex;
    std::vector<std::shared_ptr<Sink>> m_sinks;
    Level m_minLevel = Level::Debug;
};

[[nodiscard]] std::string formatEntry(const Entry& entry);

template <typename... Args>
void write(Level level, std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
    auto& logger = Logger::instance();
    if (!logger.enabled(level)) return;
    logger.log(level, category, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace lf::log

#define LF_LOG_TRACE(cat, ...) ::lf::log::write(::lf::log::Level::Trace, cat, __VA_ARGS__)
#define LF_LOG_DEBUG(cat, ...) ::lf::log::write(::lf::log::Level::Debug, cat, __VA_ARGS__)
#define LF_LOG_INFO(cat, ...)  ::lf::log::write(::lf::log::Level::Info,  cat, __VA_ARGS__)
#define LF_LOG_WARN(cat, ...)  ::lf::log::write(::lf::log::Level::Warn,  cat, __VA_ARGS__)
#define LF_LOG_ERROR(cat, ...) ::lf::log::write(::lf::log::Level::Error, cat, __VA_ARGS__)
