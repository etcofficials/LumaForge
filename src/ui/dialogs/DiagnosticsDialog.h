#pragma once

#include "lumaforge/core/Log.h"
#include "lumaforge/diagnostics/SystemInfo.h"
#include "lumaforge/hardware/HwCapabilities.h"

#include <QDialog>

#include <memory>
#include <optional>

class QLabel;
class QPlainTextEdit;
class QTabWidget;
class QTreeWidget;

namespace lf::ui {

// "System Information" window: what LumaForge detected and what it decided.
// The Graphics tab is the honest answer to "is hardware acceleration on?".
class DiagnosticsDialog : public QDialog {
    Q_OBJECT
public:
    DiagnosticsDialog(std::shared_ptr<log::RingBufferSink> logSink, QWidget* parent = nullptr);
    ~DiagnosticsDialog() override;

    void setSystemReport(const diag::SystemReport& report, const QString& qtVersion, const QString& previewBackend);
    void setHardwareReport(const hw::HwReport& report);   // may arrive later (async detection)
    void setHardwarePending();
    void selectTab(const QString& name);   // "system" | "graphics" | "log"

signals:
    void copyRequested();

private:
    QWidget* buildSystemTab();
    QWidget* buildGraphicsTab();
    QWidget* buildLogTab();
    void appendLogEntry(const log::Entry& entry);
    void copyReportToClipboard();
    [[nodiscard]] QString reportAsText() const;

    std::shared_ptr<log::RingBufferSink> m_logSink;
    QTabWidget* m_tabs = nullptr;
    QTreeWidget* m_systemTree = nullptr;
    QTreeWidget* m_graphicsTree = nullptr;
    QLabel* m_graphicsSummary = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    std::optional<diag::SystemReport> m_system;
    std::optional<hw::HwReport> m_hardware;
    QString m_qtVersion;
    QString m_previewBackend;
};

}  // namespace lf::ui
