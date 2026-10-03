#include "ui/media/ProxyController.h"

#include "lumaforge/export/Proxy.h"
#include "ui/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>

namespace lf::ui {

using project::MediaId;

ProxyController::ProxyController(project::Document& doc, QObject* parent) : QObject(parent), m_doc(doc) {
    m_use = QSettings().value(QStringLiteral("preview/useProxies"), false).toBool();
}

ProxyController::~ProxyController() {
    m_cancel = true;
    m_queue.clear();
    if (m_worker.joinable()) m_worker.join();
}

QString ProxyController::proxyPath(const project::MediaItem& item) const {
    return QDir(AppPaths::path(AppPaths::Kind::Proxies)).filePath(QString::fromStdString(exporting::proxyFileName(item)));
}

ProxyController::State ProxyController::state(MediaId id) const {
    if (id == m_current) return State::Making;
    if (std::find(m_queue.begin(), m_queue.end(), id) != m_queue.end()) return State::Queued;
    const auto* item = m_doc.project().findMedia(id);
    if (!item || !exporting::proxyApplies(*item)) return State::None;
    if (QFileInfo::exists(proxyPath(*item))) return State::Ready;
    return m_errors.contains(id) ? State::Failed : State::None;
}

double ProxyController::progress(MediaId id) const { return id == m_current ? m_progress : 0.0; }

void ProxyController::setUseProxies(bool on) {
    if (on == m_use) return;
    m_use = on;
    QSettings().setValue(QStringLiteral("preview/useProxies"), on);
    emit useProxiesChanged(on);
    emit proxiesChanged();
}

std::map<MediaId, std::string> ProxyController::overrides() const {
    std::map<MediaId, std::string> out;
    if (!m_use) return out;
    for (const auto& m : m_doc.project().media) {
        if (!exporting::proxyApplies(m) || m.offline) continue;
        const QString path = proxyPath(m);
        if (QFileInfo::exists(path)) out[m.id] = QDir::fromNativeSeparators(path).toStdString();
    }
    return out;
}

void ProxyController::create(const QList<MediaId>& ids) {
    for (auto id : ids) {
        const auto* item = m_doc.project().findMedia(id);
        if (!item || item->offline || !exporting::proxyApplies(*item)) continue;
        const State s = state(id);
        if (s == State::Queued || s == State::Making || s == State::Ready) continue;
        m_errors.remove(id);
        m_queue.push_back(id);
        emit stateChanged(id);
    }
    startNext();
}

void ProxyController::cancel(MediaId id) {
    if (id == m_current) { m_cancel = true; return; }
    const auto before = m_queue.size();
    std::erase(m_queue, id);
    if (m_queue.size() != before) emit stateChanged(id);
}

void ProxyController::remove(const QList<MediaId>& ids) {
    for (auto id : ids) {
        cancel(id);
        if (const auto* item = m_doc.project().findMedia(id)) QFile::remove(proxyPath(*item));
        m_errors.remove(id);
        emit stateChanged(id);
    }
    emit proxiesChanged();
}

void ProxyController::startNext() {
    if (project::isValid(m_current) || m_queue.empty()) return;
    if (m_worker.joinable()) m_worker.join();
    m_current = m_queue.front();
    m_queue.pop_front();
    m_progress = 0.0;
    m_cancel = false;
    const auto* item = m_doc.project().findMedia(m_current);
    if (!item) { m_current = MediaId::Invalid; startNext(); return; }
    const std::string source = item->path;
    const std::string target = QDir::fromNativeSeparators(proxyPath(*item)).toStdString();
    const MediaId id = m_current;
    emit stateChanged(id);
    QDir().mkpath(AppPaths::path(AppPaths::Kind::Proxies));
    QPointer<ProxyController> self(this);
    m_worker = std::thread([this, self, id, source, target] {
        auto result = exporting::makeProxy(source, target, exporting::kProxyHeight, [self, id](double f) {
            QMetaObject::invokeMethod(self.data(), [self, id, f] {
                if (!self || self->m_current != id) return;
                // Report in whole percent steps only.
                if (static_cast<int>(f * 100) == static_cast<int>(self->m_progress * 100)) return;
                self->m_progress = f;
                emit self->stateChanged(id);
            }, Qt::QueuedConnection);
        }, m_cancel);
        QMetaObject::invokeMethod(self.data(), [self, id, result] {
            if (!self) return;
            self->m_current = MediaId::Invalid;
            if (!result && !self->m_cancel.load()) self->m_errors.insert(id, QString::fromStdString(result.error().message));
            emit self->stateChanged(id);
            if (result) emit self->proxiesChanged();
            self->startNext();
        }, Qt::QueuedConnection);
    });
}

}  // namespace lf::ui
