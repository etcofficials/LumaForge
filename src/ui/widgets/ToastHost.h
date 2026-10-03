#pragma once

#include <QPointer>
#include <QWidget>

#include <deque>

class QLabel;
class QTimer;

namespace lf::ui {

// Non-intrusive notifications ("Project saved", "Export finished"): small
// cards stacked in the bottom-right corner of the host window that fade out
// on their own. Never modal, never steals focus, never pauses playback.
class ToastHost : public QObject {
    Q_OBJECT
public:
    enum class Kind { Info, Success, Warning, Error };

    explicit ToastHost(QWidget* host);

    void show(const QString& text, Kind kind = Kind::Info, int msec = 3500);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void relayout();
    void dismiss(QWidget* toast);

    QWidget* m_host;
    std::deque<QPointer<QWidget>> m_toasts;
    static constexpr int kMaxVisible = 4;
};

}  // namespace lf::ui
