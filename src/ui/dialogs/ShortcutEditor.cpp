#include "ui/dialogs/ShortcutEditor.h"

#include "ui/ActionRegistry.h"
#include "ui/Theme.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;

// Small modal: press the new combination.
class CaptureDialog : public QDialog {
public:
    CaptureDialog(const QString& command, QWidget* parent) : QDialog(parent) {
        setWindowTitle(tr("Change Shortcut"));
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(tr("Press the new shortcut for <b>%1</b>:").arg(command.toHtmlEscaped()), this));
        m_edit = new QKeySequenceEdit(this);
        m_edit->setMaximumSequenceLength(1);
        layout->addWidget(m_edit);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        m_edit->setFocus();
    }
    [[nodiscard]] QKeySequence sequence() const { return m_edit->keySequence(); }

private:
    QKeySequenceEdit* m_edit;
};
}  // namespace

ShortcutEditor::ShortcutEditor(ActionRegistry& actions, QWidget* parent) : QWidget(parent), m_actions(actions) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search commands or shortcuts (e.g. \"redo\" or \"Ctrl+Y\")"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut"), tr("Context")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(false);
    layout->addWidget(m_table, 1);

    auto* row = new QHBoxLayout();
    m_change = new QPushButton(tr("Change…"), this);
    m_clear = new QPushButton(tr("Clear"), this);
    m_reset = new QPushButton(tr("Reset"), this);
    auto* resetAllButton = new QPushButton(tr("Reset All"), this);
    row->addWidget(m_change);
    row->addWidget(m_clear);
    row->addWidget(m_reset);
    row->addStretch(1);
    row->addWidget(resetAllButton);
    layout->addLayout(row);

    connect(m_search, &QLineEdit::textChanged, this, &ShortcutEditor::applyFilter);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ShortcutEditor::updateButtons);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { changeCurrent(); });
    connect(m_change, &QPushButton::clicked, this, &ShortcutEditor::changeCurrent);
    connect(m_clear, &QPushButton::clicked, this, &ShortcutEditor::clearCurrent);
    connect(m_reset, &QPushButton::clicked, this, &ShortcutEditor::resetCurrent);
    connect(resetAllButton, &QPushButton::clicked, this, &ShortcutEditor::resetAll);
    connect(&m_actions, &ActionRegistry::shortcutsChanged, this, &ShortcutEditor::refill);
    refill();
}

void ShortcutEditor::refill() {
    bool ok = false;
    const ActionId keep = currentId(&ok);
    const auto conflicts = m_actions.allConflicts();
    auto inConflict = [&](ActionId id, QString* with) {
        for (const auto& [a, b] : conflicts) {
            if (a == id || b == id) { if (with) *with = m_actions.plainText(a == id ? b : a); return true; }
        }
        return false;
    };
    const auto& pal = Theme::palette();
    m_table->setRowCount(0);
    for (ActionId id : m_actions.allIds()) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        auto* cmd = new QTableWidgetItem(m_actions.plainText(id));
        cmd->setData(kIdRole, static_cast<int>(id));
        cmd->setToolTip(m_actions.description(id));
        auto* keys = new QTableWidgetItem(m_actions.allShortcutsText(id));
        if (m_actions.isCustomised(id)) { QFont f = keys->font(); f.setBold(true); keys->setFont(f); keys->setToolTip(tr("Changed from the default")); }
        QString with;
        if (inConflict(id, &with)) {
            keys->setForeground(pal.danger);
            keys->setToolTip(tr("Conflicts with \"%1\" — only one of them will run").arg(with));
        }
        auto* ctx = new QTableWidgetItem(ActionRegistry::categoryName(m_actions.category(id)));
        m_table->setItem(r, 0, cmd);
        m_table->setItem(r, 1, keys);
        m_table->setItem(r, 2, ctx);
        if (ok && id == keep) m_table->selectRow(r);
    }
    applyFilter();
    updateButtons();
}

void ShortcutEditor::applyFilter() {
    const QString q = m_search->text().trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        bool show = q.isEmpty();
        for (int c = 0; c < 3 && !show; ++c) show = m_table->item(r, c)->text().contains(q, Qt::CaseInsensitive);
        m_table->setRowHidden(r, !show);
    }
}

ActionId ShortcutEditor::currentId(bool* ok) const {
    const int r = m_table->currentRow();
    *ok = r >= 0 && m_table->item(r, 0);
    return *ok ? static_cast<ActionId>(m_table->item(r, 0)->data(kIdRole).toInt()) : ActionId::ProjectNew;
}

void ShortcutEditor::updateButtons() {
    bool ok = false;
    const ActionId id = currentId(&ok);
    m_change->setEnabled(ok);
    m_clear->setEnabled(ok && !m_actions.action(id)->shortcuts().isEmpty());
    m_reset->setEnabled(ok && m_actions.isCustomised(id));
}

void ShortcutEditor::changeCurrent() {
    bool ok = false;
    const ActionId id = currentId(&ok);
    if (!ok) return;
    CaptureDialog dlg(m_actions.plainText(id), this);
    if (dlg.exec() != QDialog::Accepted || dlg.sequence().isEmpty()) return;
    const QKeySequence seq = dlg.sequence();
    const auto others = m_actions.conflictsWith(seq, id);
    if (!others.isEmpty()) {
        QStringList names;
        for (auto o : others) names << m_actions.plainText(o);
        const auto answer = QMessageBox::question(this, tr("Shortcut In Use"),
                                                  tr("%1 is already used by: %2.\n\nAssign it to \"%3\" and remove it from the other command(s)?")
                                                      .arg(seq.toString(QKeySequence::NativeText), names.join(QStringLiteral(", ")), m_actions.plainText(id)));
        if (answer != QMessageBox::Yes) return;
        for (auto o : others) {
            auto list = m_actions.action(o)->shortcuts();
            list.removeAll(seq);
            m_actions.setShortcuts(o, list);
        }
    }
    // The new key becomes the primary shortcut; existing extra shortcuts are kept.
    auto list = m_actions.action(id)->shortcuts();
    list.removeAll(seq);
    list.prepend(seq);
    m_actions.setShortcuts(id, list);
}

void ShortcutEditor::clearCurrent() {
    bool ok = false;
    const ActionId id = currentId(&ok);
    if (ok) m_actions.setShortcuts(id, {});
}

void ShortcutEditor::resetCurrent() {
    bool ok = false;
    const ActionId id = currentId(&ok);
    if (!ok) return;
    // Restoring a default can collide with a key the user moved elsewhere.
    for (const auto& seq : m_actions.defaultShortcuts(id)) {
        if (!m_actions.conflictsWith(seq, id).isEmpty()) {
            QMessageBox::information(this, tr("Shortcut In Use"), tr("The default %1 is now used by another command. Clear it there first.").arg(seq.toString(QKeySequence::NativeText)));
            return;
        }
    }
    m_actions.resetShortcuts(id);
}

void ShortcutEditor::resetAll() {
    if (QMessageBox::question(this, tr("Reset All Shortcuts"), tr("Restore every keyboard shortcut to its default?")) != QMessageBox::Yes) return;
    m_actions.resetAllShortcuts();
}

ShortcutsDialog::ShortcutsDialog(ActionRegistry& actions, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(720, 620);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new ShortcutEditor(actions, this), 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

}  // namespace lf::ui
