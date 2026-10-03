#include "ui/home/HomeScreen.h"

#include "lumaforge/core/Version.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
QPushButton* bigButton(const QString& icon, const QString& text, const QString& hint, QWidget* parent, bool primary = false) {
    auto* b = new QPushButton(Icons::icon(icon, primary ? Theme::palette().accentText : Theme::palette().textPrimary), QStringLiteral("  ") + text, parent);
    b->setToolTip(hint);
    b->setMinimumHeight(40);
    b->setIconSize(QSize(18, 18));
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding-left: 14px; font-size: 10pt; }"));
    if (primary) b->setProperty("primary", true);
    return b;
}
}  // namespace

HomeScreen::HomeScreen(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("homeScreen"));
    setAttribute(Qt::WA_StyledBackground, true);
    const auto& pal = Theme::palette();

    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(48, 40, 48, 40);
    outer->addStretch(1);

    // Left: brand + actions
    auto* left = new QVBoxLayout();
    left->setSpacing(10);
    auto* logoRow = new QHBoxLayout();
    auto* logo = new QLabel(this);
    logo->setPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(56, 56));
    logoRow->addWidget(logo);
    auto* brand = new QVBoxLayout();
    auto* title = new QLabel(QStringLiteral("LUMAFORGE"), this);
    title->setStyleSheet(QStringLiteral("font-size: 26pt; font-weight: 600; letter-spacing: 4px; color: %1;").arg(pal.textPrimary.name()));
    auto* tagline = new QLabel(tr("Lightweight professional video editor  ·  v%1").arg(QString::fromLatin1(version::string)), this);
    tagline->setStyleSheet(QStringLiteral("color: %1;").arg(pal.textSecondary.name()));
    brand->addWidget(title);
    brand->addWidget(tagline);
    logoRow->addLayout(brand);
    logoRow->addStretch(1);
    left->addLayout(logoRow);
    left->addSpacing(24);

    auto* newBtn = bigButton(QStringLiteral("new"), tr("New Project"), tr("Start a new project (Ctrl+N)"), this, true);
    auto* openBtn = bigButton(QStringLiteral("open"), tr("Open Project…"), tr("Open a saved .lfproj project (Ctrl+O)"), this);
    auto* importBtn = bigButton(QStringLiteral("import"), tr("Import Media…"), tr("Start a new project with media files"), this);
    for (auto* b : {newBtn, openBtn, importBtn}) { b->setFixedWidth(280); left->addWidget(b); }
    connect(newBtn, &QPushButton::clicked, this, &HomeScreen::newProjectRequested);
    connect(openBtn, &QPushButton::clicked, this, &HomeScreen::openProjectRequested);
    connect(importBtn, &QPushButton::clicked, this, &HomeScreen::importMediaRequested);
    left->addStretch(1);
    auto* hint = new QLabel(tr("Tip: drop video files anywhere on this window to start editing."), this);
    hint->setStyleSheet(QStringLiteral("color: %1;").arg(pal.textMuted.name()));
    left->addWidget(hint);
    outer->addLayout(left);
    outer->addSpacing(56);

    // Right: recovery banner + recent projects
    auto* right = new QVBoxLayout();
    right->setSpacing(8);
    m_recoveryBanner = new QFrame(this);
    m_recoveryBanner->setObjectName(QStringLiteral("recoveryBanner"));
    m_recoveryBanner->setStyleSheet(QStringLiteral("#recoveryBanner { background: %1; border: 1px solid %2; border-radius: 4px; }")
                                        .arg(pal.bgElevated.name(), pal.warning.name()));
    auto* bannerRow = new QHBoxLayout(m_recoveryBanner);
    bannerRow->setContentsMargins(12, 10, 12, 10);
    m_recoveryText = new QLabel(m_recoveryBanner);
    m_recoveryText->setWordWrap(true);
    bannerRow->addWidget(m_recoveryText, 1);
    auto* openRec = new QPushButton(tr("Open Recovery"), m_recoveryBanner);
    openRec->setProperty("primary", true);
    auto* discardRec = new QPushButton(tr("Discard Recovery"), m_recoveryBanner);
    bannerRow->addWidget(openRec);
    bannerRow->addWidget(discardRec);
    connect(openRec, &QPushButton::clicked, this, [this] { emit openRecoveryRequested(m_recoveryPath); });
    connect(discardRec, &QPushButton::clicked, this, [this] { emit discardRecoveryRequested(m_recoveryPath); });
    m_recoveryBanner->hide();
    right->addWidget(m_recoveryBanner);

    auto* recentHeader = new QHBoxLayout();
    auto* recentTitle = new QLabel(tr("Recent Projects"), this);
    recentTitle->setStyleSheet(QStringLiteral("font-size: 11pt; font-weight: 600; color: %1;").arg(pal.textPrimary.name()));
    recentHeader->addWidget(recentTitle);
    recentHeader->addStretch(1);
    m_clearRecent = new QPushButton(tr("Clear List"), this);
    m_clearRecent->setFlat(true);
    m_clearRecent->setCursor(Qt::PointingHandCursor);
    connect(m_clearRecent, &QPushButton::clicked, this, &HomeScreen::clearRecentRequested);
    recentHeader->addWidget(m_clearRecent);
    right->addLayout(recentHeader);

    m_recent = new QListWidget(this);
    m_recent->setMinimumWidth(460);
    m_recent->setIconSize(QSize(20, 20));
    m_recent->setUniformItemSizes(true);
    m_recent->setStyleSheet(QStringLiteral("QListWidget { background: %1; border: 1px solid %2; border-radius: 4px; padding: 4px; }"
                                           "QListWidget::item { padding: 6px; border-radius: 3px; }"
                                           "QListWidget::item:hover { background: %3; }")
                                .arg(pal.bgPanel.name(), pal.border.name(), pal.bgHover.name()));
    connect(m_recent, &QListWidget::itemActivated, this, &HomeScreen::activate);
    right->addWidget(m_recent, 1);
    m_recentEmpty = new QLabel(tr("No recent projects yet. Projects you open or save appear here."), this);
    m_recentEmpty->setStyleSheet(QStringLiteral("color: %1;").arg(pal.textMuted.name()));
    right->addWidget(m_recentEmpty);
    outer->addLayout(right, 0);
    outer->addStretch(1);
}

void HomeScreen::setRecentProjects(const QStringList& paths) {
    m_recent->clear();
    const QLocale locale;
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        const bool exists = fi.exists();
        auto* item = new QListWidgetItem(Icons::icon(QStringLiteral("open")), QString(), m_recent);
        const QString when = exists ? locale.toString(fi.lastModified(), QLocale::ShortFormat) : tr("missing");
        item->setText(QStringLiteral("%1\n%2  ·  %3").arg(fi.completeBaseName(), QDir::toNativeSeparators(fi.absolutePath()), when));
        item->setData(Qt::UserRole, p);
        item->setToolTip(exists ? tr("Open %1").arg(p) : tr("%1 can no longer be found").arg(p));
        if (!exists) item->setForeground(Theme::palette().textDisabled);
    }
    m_recent->setVisible(!paths.isEmpty());
    m_recentEmpty->setVisible(paths.isEmpty());
    m_clearRecent->setVisible(!paths.isEmpty());
}

void HomeScreen::setRecovery(const QString& autosavePath, const QString& projectName, const QDateTime& when) {
    m_recoveryPath = autosavePath;
    m_recoveryText->setText(tr("<b>Recovered project available</b><br>LumaForge did not close normally. An autosave of "
                               "\"%1\" from %2 can be opened. Your original project file has not been changed.")
                                .arg(projectName.toHtmlEscaped(), QLocale().toString(when, QLocale::ShortFormat)));
    m_recoveryBanner->show();
}

void HomeScreen::clearRecovery() {
    m_recoveryPath.clear();
    m_recoveryBanner->hide();
}

void HomeScreen::activate(QListWidgetItem* item) {
    if (!item) return;
    emit openRecentRequested(item->data(Qt::UserRole).toString());
}

void HomeScreen::paintEvent(QPaintEvent* e) {
    QPainter p(this);
    p.fillRect(rect(), Theme::palette().bgWindow);
    QWidget::paintEvent(e);
}

}  // namespace lf::ui
