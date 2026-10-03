#include "ui/widgets/ToastHost.h"

#include "ui/Theme.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>

namespace lf::ui {

ToastHost::ToastHost(QWidget* host) : QObject(host), m_host(host) { host->installEventFilter(this); }

void ToastHost::show(const QString& text, Kind kind, int msec) {
    const auto& pal = Theme::palette();
    QColor accent = pal.accent;
    switch (kind) {
        case Kind::Success: accent = pal.success; break;
        case Kind::Warning: accent = pal.warning; break;
        case Kind::Error: accent = pal.danger; break;
        case Kind::Info: break;
    }
    auto* toast = new QWidget(m_host);
    toast->setAttribute(Qt::WA_StyledBackground, true);
    toast->setAttribute(Qt::WA_TransparentForMouseEvents, kind != Kind::Error);   // errors can be clicked away
    toast->setObjectName(QStringLiteral("toast"));
    toast->setStyleSheet(QStringLiteral("#toast { background: %1; border: 1px solid %2; border-left: 3px solid %3; border-radius: 4px; }"
                                        "#toast QLabel { color: %4; background: transparent; }")
                             .arg(pal.bgElevated.name(), pal.borderStrong.name(), accent.name(), pal.textPrimary.name()));
    auto* row = new QHBoxLayout(toast);
    row->setContentsMargins(12, 8, 14, 8);
    auto* label = new QLabel(text, toast);
    label->setWordWrap(true);
    label->setMaximumWidth(360);
    row->addWidget(label);
    toast->adjustSize();
    if (kind == Kind::Error) toast->installEventFilter(this);
    toast->show();
    toast->raise();

    m_toasts.push_back(toast);
    while (m_toasts.size() > kMaxVisible) {
        if (m_toasts.front()) m_toasts.front()->deleteLater();
        m_toasts.pop_front();
    }
    relayout();
    QPointer<QWidget> guard(toast);
    QTimer::singleShot(msec, this, [this, guard] { if (guard) dismiss(guard); });
}

void ToastHost::dismiss(QWidget* toast) {
    auto* effect = new QGraphicsOpacityEffect(toast);
    toast->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", toast);
    anim->setDuration(250);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    QPointer<QWidget> guard(toast);
    connect(anim, &QPropertyAnimation::finished, this, [this, guard] {
        if (!guard) return;
        std::erase_if(m_toasts, [&](const QPointer<QWidget>& p) { return p == guard || !p; });
        guard->deleteLater();
        relayout();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToastHost::relayout() {
    int bottom = m_host->height() - 34;   // above the status bar
    for (auto it = m_toasts.rbegin(); it != m_toasts.rend(); ++it) {
        QWidget* t = *it;
        if (!t) continue;
        t->adjustSize();
        bottom -= t->height();
        t->move(m_host->width() - t->width() - 16, bottom);
        bottom -= 8;
    }
}

bool ToastHost::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_host && event->type() == QEvent::Resize) relayout();
    if (event->type() == QEvent::MouseButtonPress) {
        for (const auto& t : m_toasts) if (t && watched == t) { dismiss(t); return true; }
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace lf::ui
