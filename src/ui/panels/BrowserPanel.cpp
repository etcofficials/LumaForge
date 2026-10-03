#include "ui/panels/BrowserPanel.h"

#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace lf::ui {

BrowserPanel::BrowserPanel(QWidget* parent) : QWidget(parent) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setElideMode(Qt::ElideNone);
    m_tabs->setObjectName(QStringLiteral("browserTabs"));
    layout->addWidget(m_tabs);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        if (i >= 0 && i < m_keys.size()) QSettings().setValue(QStringLiteral("browser/tab"), m_keys[i]);
    });
}

void BrowserPanel::addPage(QWidget* page, const QString& title, const QString& key, const QString& toolTip) {
    const int i = m_tabs->addTab(page, title);
    if (!toolTip.isEmpty()) m_tabs->setTabToolTip(i, toolTip);
    m_keys << key;
    if (QSettings().value(QStringLiteral("browser/tab")).toString() == key) m_tabs->setCurrentIndex(i);
}

void BrowserPanel::showPage(const QString& key) {
    const int i = m_keys.indexOf(key);
    if (i >= 0) m_tabs->setCurrentIndex(i);
}

QString BrowserPanel::currentKey() const {
    const int i = m_tabs->currentIndex();
    return i >= 0 && i < m_keys.size() ? m_keys[i] : QString();
}

}  // namespace lf::ui
