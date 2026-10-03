#pragma once

#include "lumaforge/project/Document.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <atomic>
#include <deque>
#include <map>
#include <thread>

namespace lf::ui {

// Makes proxies one at a time on a worker thread and answers "is there a
// proxy for this media?" from the proxy cache folder. Proxies are only ever
// used for the preview; exports read the original files.
class ProxyController : public QObject {
    Q_OBJECT
public:
    enum class State { None, Queued, Making, Ready, Failed };

    explicit ProxyController(project::Document& doc, QObject* parent = nullptr);
    ~ProxyController() override;

    void create(const QList<project::MediaId>& ids);
    void cancel(project::MediaId id);
    void remove(const QList<project::MediaId>& ids);   // deletes the proxy files

    [[nodiscard]] State state(project::MediaId id) const;
    [[nodiscard]] double progress(project::MediaId id) const;   // 0..1 while making
    [[nodiscard]] QString proxyPath(const project::MediaItem& item) const;
    [[nodiscard]] QString errorText(project::MediaId id) const { return m_errors.value(id); }

    [[nodiscard]] bool useProxies() const noexcept { return m_use; }
    void setUseProxies(bool on);
    // Proxy file per media item that has one (for the preview program).
    [[nodiscard]] std::map<project::MediaId, std::string> overrides() const;

signals:
    void stateChanged(lf::project::MediaId id);
    void useProxiesChanged(bool on);
    void proxiesChanged();   // a proxy appeared or went away: the preview program should be rebuilt

private:
    void startNext();

    project::Document& m_doc;
    bool m_use = false;
    std::deque<project::MediaId> m_queue;
    project::MediaId m_current = project::MediaId::Invalid;
    double m_progress = 0.0;
    QHash<project::MediaId, QString> m_errors;
    std::atomic<bool> m_cancel{false};
    std::thread m_worker;
};

}  // namespace lf::ui
