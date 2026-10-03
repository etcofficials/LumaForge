#include "ui/dialogs/CommandPalette.h"

#include "ui/ActionRegistry.h"
#include "ui/Theme.h"

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace lf::ui {

namespace {
constexpr const char* kRecentKey = "palette/recent";
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kEnabledRole = Qt::UserRole + 2;
}

CommandPalette::CommandPalette(ActionRegistry& actions, QWidget* parent) : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint), m_actions(actions) {
    setObjectName(QStringLiteral("commandPalette"));
    const auto& pal = Theme::palette();
    setStyleSheet(QStringLiteral("#commandPalette { background: %1; border: 1px solid %2; border-radius: 6px; }"
                                 "#commandPalette QLineEdit { font-size: 11pt; padding: 8px; border: none; border-bottom: 1px solid %2; background: %3; }"
                                 "#commandPalette QListWidget { border: none; background: transparent; }"
                                 "#commandPalette QListWidget::item { padding: 5px 8px; }"
                                 "#commandPalette QListWidget::item:selected { background: %4; }")
                      .arg(pal.bgElevated.name(), pal.borderStrong.name(), pal.bgInput.name(), pal.selection.name()));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Type a command…  (Enter runs it, Esc closes)"));
    m_search->installEventFilter(this);
    layout->addWidget(m_search);
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_list);
    auto* hint = new QLabel(tr("Commands run exactly like their menu items. Greyed commands are unavailable right now."), this);
    hint->setProperty("muted", true);
    hint->setContentsMargins(10, 4, 10, 6);
    layout->addWidget(hint);
    resize(620, 420);

    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::refill);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { runCurrent(); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { runCurrent(); });
}

int CommandPalette::score(const QString& queryIn, const QString& textIn) {
    const QString query = queryIn.trimmed().toLower();
    const QString text = textIn.toLower();
    if (query.isEmpty()) return 0;
    if (text.startsWith(query)) return 1000 - static_cast<int>(text.size());
    if (const int i = text.indexOf(query); i >= 0) {
        const bool wordStart = i == 0 || !text[i - 1].isLetterOrNumber();
        return (wordStart ? 800 : 600) - i;
    }
    // Subsequence ("rpd" -> "Ripple Delete"); rewards matches at word starts.
    int qi = 0, s = 0;
    for (int i = 0; i < text.size() && qi < query.size(); ++i) {
        if (text[i] == query[qi]) {
            s += (i == 0 || !text[i - 1].isLetterOrNumber()) ? 10 : 2;
            ++qi;
        }
    }
    return qi == query.size() ? s : -1;
}

void CommandPalette::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    if (parentWidget()) {
        const QRect pr = parentWidget()->geometry();
        move(pr.center().x() - width() / 2, pr.top() + 80);
    }
    m_search->clear();
    refill();
    m_search->setFocus();
}

void CommandPalette::refill() {
    m_list->clear();
    const QString query = m_search->text();
    const QStringList recent = QSettings().value(QString::fromLatin1(kRecentKey)).toStringList();
    struct Row { ActionId id; int score; };
    std::vector<Row> rows;
    for (ActionId id : m_actions.allIds()) {
        QAction* a = m_actions.action(id);
        if (!a) continue;
        const QString label = m_actions.plainText(id);
        const QString cat = ActionRegistry::categoryName(m_actions.category(id));
        int s = std::max(score(query, label), score(query, cat + QLatin1Char(' ') + label) - 50);
        if (s < 0) s = score(query, m_actions.description(id)) >= 600 ? 100 : -1;   // "split" also finds descriptions mentioning it
        if (s < 0) continue;
        const int r = recent.indexOf(a->objectName());
        if (r >= 0) s += query.isEmpty() ? 2000 - r : 50;
        if (!a->isEnabled()) s -= 300;
        rows.push_back({id, s});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.score > b.score; });
    const auto& pal = Theme::palette();
    for (const Row& row : rows) {
        QAction* a = m_actions.action(row.id);
        QString label = m_actions.plainText(row.id);
        if (a->isCheckable()) label += a->isChecked() ? tr("  (on)") : tr("  (off)");
        const QString keys = m_actions.allShortcutsText(row.id);
        auto* item = new QListWidgetItem(QStringLiteral("%1   —   %2%3").arg(label, ActionRegistry::categoryName(m_actions.category(row.id)),
                                                                          keys.isEmpty() ? QString() : QStringLiteral("      [%1]").arg(keys)));
        item->setData(kIdRole, static_cast<int>(row.id));
        item->setData(kEnabledRole, a->isEnabled());
        item->setToolTip(a->isEnabled() ? m_actions.description(row.id) : tr("Unavailable right now: %1").arg(m_actions.description(row.id)));
        if (!a->isEnabled()) item->setForeground(pal.textDisabled);
        if (!a->icon().isNull()) item->setIcon(a->icon());
        m_list->addItem(item);
    }
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kEnabledRole).toBool()) { m_list->setCurrentRow(i); break; }
    }
}

void CommandPalette::runCurrent() {
    QListWidgetItem* item = m_list->currentItem();
    if (!item || !item->data(kEnabledRole).toBool()) return;
    const auto id = static_cast<ActionId>(item->data(kIdRole).toInt());
    QAction* a = m_actions.action(id);
    if (!a) return;
    QSettings s;
    QStringList recent = s.value(QString::fromLatin1(kRecentKey)).toStringList();
    recent.removeAll(a->objectName());
    recent.prepend(a->objectName());
    while (recent.size() > 12) recent.removeLast();
    s.setValue(QString::fromLatin1(kRecentKey), recent);
    accept();
    // Run after the popup is gone so commands that open dialogs get a clean focus chain.
    QTimer::singleShot(0, a, &QAction::trigger);
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        const int row = m_list->currentRow();
        switch (ke->key()) {
            case Qt::Key_Down:
                m_list->setCurrentRow(std::min(m_list->count() - 1, row + 1));
                return true;
            case Qt::Key_Up:
                m_list->setCurrentRow(std::max(0, row - 1));
                return true;
            case Qt::Key_PageDown:
                m_list->setCurrentRow(std::min(m_list->count() - 1, row + 10));
                return true;
            case Qt::Key_PageUp:
                m_list->setCurrentRow(std::max(0, row - 10));
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                runCurrent();
                return true;
            case Qt::Key_Escape:
                reject();
                return true;
            default:
                break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

}  // namespace lf::ui
