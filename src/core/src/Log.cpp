#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"

#include <windows.h>

#include <cstdio>
#include <ctime>
#include <share.h>

namespace lf::log {

// ---------------------------------------------------------------- formatting

std::string formatEntry(const Entry& e) {
    using namespace std::chrono;
    const auto t = system_clock::to_time_t(e.time);
    const auto ms = duration_cast<milliseconds>(e.time.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_s(&tm, &t);
    return std::format("{:02}:{:02}:{:02}.{:03} [{:5}] [{:>6}] {:<8} {}", tm.tm_hour, tm.tm_min, tm.tm_sec,
                       static_cast<int>(ms.count()), levelName(e.level), e.threadId, e.category, e.message);
}

// ---------------------------------------------------------------- RingBufferSink

void RingBufferSink::write(const Entry& entry) {
    std::function<void(const Entry&)> listener;
    {
        std::lock_guard lock(m_mutex);
        m_entries.push_back(entry);
        while (m_entries.size() > m_capacity) m_entries.pop_front();
        listener = m_listener;
    }
    if (listener) listener(entry);
}

std::vector<Entry> RingBufferSink::snapshot() const {
    std::lock_guard lock(m_mutex);
    return {m_entries.begin(), m_entries.end()};
}

void RingBufferSink::setListener(std::function<void(const Entry&)> listener) {
    std::lock_guard lock(m_mutex);
    m_listener = std::move(listener);
}

// ---------------------------------------------------------------- FileSink

FileSink::FileSink(const std::wstring& path) {
    // Deny other writers only, so the log can be tailed while LumaForge runs.
    m_file = _wfsopen(path.c_str(), L"w, ccs=UTF-8", _SH_DENYWR);
}

FileSink::~FileSink() {
    if (m_file) std::fclose(static_cast<FILE*>(m_file));
}

void FileSink::write(const Entry& entry) {
    if (!m_file) return;
    const auto line = formatEntry(entry) + "\n";
    const auto wide = utf8ToWide(line);
    std::lock_guard lock(m_mutex);
    std::fputws(wide.c_str(), static_cast<FILE*>(m_file));
    if (entry.level >= Level::Warn) std::fflush(static_cast<FILE*>(m_file));
}

void FileSink::flush() {
    std::lock_guard lock(m_mutex);
    if (m_file) std::fflush(static_cast<FILE*>(m_file));
}

// ---------------------------------------------------------------- DebuggerSink

void DebuggerSink::write(const Entry& entry) {
    if (!IsDebuggerPresent()) return;
    OutputDebugStringW(utf8ToWide(formatEntry(entry) + "\n").c_str());
}

// ---------------------------------------------------------------- Logger

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::addSink(std::shared_ptr<Sink> sink) {
    std::lock_guard lock(m_sinkMutex);
    m_sinks.push_back(std::move(sink));
}

void Logger::log(Level level, std::string_view category, std::string message) {
    Entry entry{std::chrono::system_clock::now(), level, static_cast<std::uint32_t>(GetCurrentThreadId()),
                std::string(category), std::move(message)};
    std::lock_guard lock(m_sinkMutex);
    for (auto& sink : m_sinks) sink->write(entry);
}

void Logger::flush() {
    std::lock_guard lock(m_sinkMutex);
    for (auto& sink : m_sinks) sink->flush();
}

}  // namespace lf::log
