#pragma once

#include "lumaforge/hardware/HwCapabilities.h"
#include "lumaforge/playback/PlaybackEngine.h"
#include "lumaforge/project/Document.h"

#include <QWidget>

#include <optional>

class QLabel;
class QPushButton;
class QTimer;

namespace lf::ui {

class ActionRegistry;
class PlaybackController;
class ProxyController;

// What limits playback right now and what to do about it: live preview
// numbers, process CPU / memory, the media this GPU cannot decode (with a
// one-click proxy fix), and the size of every cache folder with a way to
// clear it.
class PerformancePanel : public QWidget {
    Q_OBJECT
public:
    PerformancePanel(project::Document& doc, PlaybackController& playback, ProxyController& proxies, ActionRegistry& actions, QWidget* parent = nullptr);
    ~PerformancePanel() override;

    void setHardwareReport(const hw::HwReport& report);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void sample();
    void analyseMedia();
    void refreshCaches();

    project::Document& m_doc;
    PlaybackController& m_playback;
    ProxyController& m_proxies;
    int m_listenerToken = 0;
    std::optional<hw::HwReport> m_report;
    QLabel* m_live;
    QLabel* m_process;
    QLabel* m_mediaSummary;
    QPushButton* m_makeProxies;
    QLabel* m_caches;
    QTimer* m_timer;
    QList<project::MediaId> m_heavy;
    std::uint64_t m_lastCpu = 0;
    std::uint64_t m_lastWall = 0;
};

}  // namespace lf::ui
