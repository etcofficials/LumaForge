#include "ui/workspace/WorkspaceManager.h"

#include <QCoreApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QSettings>
#include <QTabBar>

namespace lf::ui {

namespace {
constexpr int kLayoutVersion = 3;
constexpr Workspace kAll[] = {Workspace::Edit, Workspace::Color, Workspace::Audio, Workspace::Text, Workspace::Effects, Workspace::Export};
}  // namespace

QString workspaceName(Workspace w) {
    switch (w) {
        case Workspace::Edit: return QCoreApplication::translate("Workspace", "EDIT");
        case Workspace::Color: return QCoreApplication::translate("Workspace", "COLOR");
        case Workspace::Audio: return QCoreApplication::translate("Workspace", "AUDIO");
        case Workspace::Text: return QCoreApplication::translate("Workspace", "TEXT");
        case Workspace::Effects: return QCoreApplication::translate("Workspace", "EFFECTS");
        case Workspace::Export: return QCoreApplication::translate("Workspace", "EXPORT");
    }
    return {};
}

QString workspaceKey(Workspace w) {
    switch (w) {
        case Workspace::Edit: return QStringLiteral("edit");
        case Workspace::Color: return QStringLiteral("color");
        case Workspace::Audio: return QStringLiteral("audio");
        case Workspace::Text: return QStringLiteral("text");
        case Workspace::Effects: return QStringLiteral("effects");
        case Workspace::Export: return QStringLiteral("export");
    }
    return {};
}

WorkspaceManager::WorkspaceManager(QMainWindow* editor, QTabBar* bar, QObject* parent) : QObject(parent), m_editor(editor), m_bar(bar) {
    for (Workspace w : kAll) {
        const int i = m_bar->addTab(workspaceName(w));
        m_bar->setTabData(i, static_cast<int>(w));
        m_bar->setTabVisible(i, false);
    }
    connect(m_bar, &QTabBar::currentChanged, this, [this](int i) {
        if (m_switching || i < 0) return;
        switchTo(static_cast<Workspace>(m_bar->tabData(i).toInt()));
    });
}

int WorkspaceManager::tabIndex(Workspace w) const {
    for (int i = 0; i < m_bar->count(); ++i) if (m_bar->tabData(i).toInt() == static_cast<int>(w)) return i;
    return -1;
}

void WorkspaceManager::registerDock(DockId id, QDockWidget* dock) { m_docks[id] = dock; }

QDockWidget* WorkspaceManager::dock(DockId id) const {
    auto it = m_docks.find(id);
    return it == m_docks.end() ? nullptr : it->second;
}

void WorkspaceManager::setAvailable(Workspace w, bool available) {
    m_available[static_cast<int>(w)] = available;
    m_switching = true;
    m_bar->setTabVisible(tabIndex(w), available);
    m_switching = false;
}

bool WorkspaceManager::isAvailable(Workspace w) const { return m_available.value(static_cast<int>(w), false); }

static QString signatureOf(const std::map<DockId, QDockWidget*>& docks) {
    QString s;
    for (const auto& [id, d] : docks) s += QString::number(static_cast<int>(id)) + QLatin1Char(',');
    return s;
}

void WorkspaceManager::saveState(Workspace w) {
    QSettings s;
    s.beginGroup(QStringLiteral("workspaces/") + workspaceKey(w));
    s.setValue(QStringLiteral("state"), m_editor->saveState(kLayoutVersion));
    s.setValue(QStringLiteral("docks"), signatureOf(m_docks));
    s.endGroup();
}

bool WorkspaceManager::restoreState(Workspace w) {
    QSettings s;
    s.beginGroup(QStringLiteral("workspaces/") + workspaceKey(w));
    const QByteArray state = s.value(QStringLiteral("state")).toByteArray();
    const QString sig = s.value(QStringLiteral("docks")).toString();
    s.endGroup();
    // A layout saved before a panel existed would leave the new dock unplaced.
    if (state.isEmpty() || sig != signatureOf(m_docks)) return false;
    return m_editor->restoreState(state, kLayoutVersion);
}

void WorkspaceManager::switchTo(Workspace w) {
    if (!isAvailable(w)) w = Workspace::Edit;
    if (w != m_current) saveState(m_current);
    m_current = w;
    if (!restoreState(w)) applyDefault(w);
    m_switching = true;
    m_bar->setCurrentIndex(tabIndex(w));
    m_switching = false;
    emit workspaceChanged(w);
}

void WorkspaceManager::resetCurrent() {
    applyDefault(m_current);
    saveState(m_current);
    emit workspaceChanged(m_current);
}

void WorkspaceManager::restoreSession() {
    const QString key = QSettings().value(QStringLiteral("workspaces/current"), workspaceKey(Workspace::Edit)).toString();
    Workspace target = Workspace::Edit;
    for (Workspace w : kAll) if (workspaceKey(w) == key) target = w;
    m_current = target;
    switchTo(target);
}

void WorkspaceManager::saveSession() {
    saveState(m_current);
    QSettings().setValue(QStringLiteral("workspaces/current"), workspaceKey(m_current));
}

void WorkspaceManager::applyDefault(Workspace w) {
    auto place = [&](DockId id, Qt::DockWidgetArea area, bool visible) {
        QDockWidget* d = dock(id);
        if (!d) return;
        d->setFloating(false);
        m_editor->addDockWidget(area, d);
        d->setVisible(visible);
    };
    QDockWidget* browser = dock(DockId::Browser);
    QDockWidget* inspector = dock(DockId::Inspector);
    QDockWidget* timeline = dock(DockId::Timeline);
    QDockWidget* mixer = dock(DockId::Mixer);
    QDockWidget* scopes = dock(DockId::Scopes);
    QDockWidget* exportSettings = dock(DockId::ExportSettings);
    QDockWidget* exportQueue = dock(DockId::ExportQueue);
    QDockWidget* perf = dock(DockId::Performance);

    const int totalW = std::max(1000, m_editor->width());
    const int totalH = std::max(600, m_editor->height());
    switch (w) {
        case Workspace::Edit:
        case Workspace::Text:
        case Workspace::Effects:
            place(DockId::Browser, Qt::LeftDockWidgetArea, true);
            place(DockId::Inspector, Qt::RightDockWidgetArea, true);
            place(DockId::Timeline, Qt::BottomDockWidgetArea, true);
            place(DockId::Mixer, Qt::RightDockWidgetArea, false);
            place(DockId::Scopes, Qt::RightDockWidgetArea, false);
            place(DockId::ExportSettings, Qt::RightDockWidgetArea, false);
            place(DockId::ExportQueue, Qt::BottomDockWidgetArea, false);
            place(DockId::Performance, Qt::RightDockWidgetArea, false);
            if (browser && inspector) m_editor->resizeDocks({browser, inspector}, {std::max(280, totalW / 5), std::max(320, totalW / 4)}, Qt::Horizontal);
            if (timeline) m_editor->resizeDocks({timeline}, {totalH * 2 / 5}, Qt::Vertical);
            break;
        case Workspace::Color:
            place(DockId::Browser, Qt::LeftDockWidgetArea, false);
            place(DockId::Scopes, Qt::LeftDockWidgetArea, scopes != nullptr);
            place(DockId::Inspector, Qt::RightDockWidgetArea, true);
            place(DockId::Timeline, Qt::BottomDockWidgetArea, true);
            place(DockId::Mixer, Qt::RightDockWidgetArea, false);
            place(DockId::ExportSettings, Qt::RightDockWidgetArea, false);
            place(DockId::ExportQueue, Qt::BottomDockWidgetArea, false);
            place(DockId::Performance, Qt::RightDockWidgetArea, false);
            if (scopes && inspector) m_editor->resizeDocks({scopes, inspector}, {std::max(320, totalW / 4), std::max(360, totalW / 4)}, Qt::Horizontal);
            else if (inspector) m_editor->resizeDocks({inspector}, {std::max(360, totalW / 4)}, Qt::Horizontal);
            if (timeline) m_editor->resizeDocks({timeline}, {totalH / 4}, Qt::Vertical);
            break;
        case Workspace::Audio:
            place(DockId::Browser, Qt::LeftDockWidgetArea, true);
            place(DockId::Inspector, Qt::RightDockWidgetArea, true);
            place(DockId::Timeline, Qt::BottomDockWidgetArea, true);
            place(DockId::Mixer, Qt::BottomDockWidgetArea, mixer != nullptr);
            if (mixer && timeline) m_editor->splitDockWidget(timeline, mixer, Qt::Horizontal);
            place(DockId::Scopes, Qt::RightDockWidgetArea, false);
            place(DockId::ExportSettings, Qt::RightDockWidgetArea, false);
            place(DockId::ExportQueue, Qt::BottomDockWidgetArea, false);
            place(DockId::Performance, Qt::RightDockWidgetArea, false);
            if (timeline) m_editor->resizeDocks({timeline}, {totalH / 2}, Qt::Vertical);
            if (mixer && timeline) m_editor->resizeDocks({timeline, mixer}, {totalW * 2 / 3, totalW / 3}, Qt::Horizontal);
            if (browser && inspector) m_editor->resizeDocks({browser, inspector}, {std::max(260, totalW / 6), std::max(320, totalW / 4)}, Qt::Horizontal);
            break;
        case Workspace::Export:
            place(DockId::Browser, Qt::LeftDockWidgetArea, false);
            place(DockId::Inspector, Qt::RightDockWidgetArea, false);
            place(DockId::Mixer, Qt::RightDockWidgetArea, false);
            place(DockId::Scopes, Qt::RightDockWidgetArea, false);
            place(DockId::Performance, Qt::RightDockWidgetArea, false);
            place(DockId::ExportSettings, Qt::RightDockWidgetArea, true);
            place(DockId::Timeline, Qt::BottomDockWidgetArea, true);
            place(DockId::ExportQueue, Qt::BottomDockWidgetArea, true);
            if (exportQueue && timeline) m_editor->splitDockWidget(timeline, exportQueue, Qt::Horizontal);
            if (exportSettings) m_editor->resizeDocks({exportSettings}, {std::max(420, totalW / 3)}, Qt::Horizontal);
            if (timeline) m_editor->resizeDocks({timeline}, {totalH / 3}, Qt::Vertical);
            break;
    }
    (void)perf; (void)exportQueue;
}

}  // namespace lf::ui
