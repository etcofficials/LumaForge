#include "ui/dialogs/SettingsDialog.h"

#include "ui/AppPaths.h"
#include "ui/dialogs/ShortcutEditor.h"
#include "ui/media/ProxyController.h"
#include "ui/project/AutosaveManager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
QLineEdit* folderField(QWidget* parent, QFormLayout* form, const QString& label, const QString& placeholder) {
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* edit = new QLineEdit(row);
    edit->setPlaceholderText(placeholder);
    auto* browse = new QToolButton(row);
    browse->setText(QObject::tr("Browse…"));
    h->addWidget(edit, 1);
    h->addWidget(browse);
    QObject::connect(browse, &QToolButton::clicked, row, [edit, row, label] {
        const QString dir = QFileDialog::getExistingDirectory(row, label, edit->text().isEmpty() ? edit->placeholderText() : edit->text());
        if (!dir.isEmpty()) edit->setText(QDir::toNativeSeparators(dir));
    });
    form->addRow(label, row);
    return edit;
}

QLabel* note(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setProperty("muted", true);
    return l;
}
}  // namespace

double SettingsDialog::defaultTransitionSeconds() { return QSettings().value(QStringLiteral("editing/transitionSeconds"), 1.0).toDouble(); }
bool SettingsDialog::hardwareDecodeAllowed() { return QSettings().value(QStringLiteral("preview/hardwareDecode"), true).toBool(); }

SettingsDialog::SettingsDialog(ActionRegistry& actions, AutosaveManager& autosave, ProxyController& proxies, QWidget* parent)
    : QDialog(parent), m_autosave(autosave), m_proxies(proxies) {
    setWindowTitle(tr("Settings"));
    resize(820, 560);
    auto* layout = new QVBoxLayout(this);
    auto* body = new QHBoxLayout();
    m_nav = new QListWidget(this);
    m_nav->setObjectName(QStringLiteral("settingsNav"));
    m_nav->setFixedWidth(160);
    m_pages = new QStackedWidget(this);
    body->addWidget(m_nav);
    body->addWidget(m_pages, 1);
    layout->addLayout(body, 1);
    const std::pair<QString, QWidget*> pages[] = {{tr("General"), generalPage()}, {tr("Preview"), previewPage()}, {tr("Editing"), editingPage()},
                                                  {tr("Export"), exportPage()}, {tr("Keyboard"), keyboardPage(actions)}};
    for (const auto& [name, page] : pages) {
        m_nav->addItem(name);
        m_pages->addWidget(page);
    }
    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { apply(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::apply);
    load();
}

QWidget* SettingsDialog::generalPage() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    auto* form = new QFormLayout();
    v->addLayout(form);
    m_dataRoot = folderField(page, form, tr("Data folder"), AppPaths::defaultDataRoot());
    m_dataRoot->setObjectName(QStringLiteral("settingsDataRoot"));
    m_dataWarning = new QLabel(page);
    m_dataWarning->setWordWrap(true);
    form->addRow(QString(), m_dataWarning);
    form->addRow(QString(), note(page, tr("Projects, exports, proxies, caches, autosaves, recordings, backups and logs live here (never in your user profile). Takes effect after a restart; existing files are not moved.")));
    connect(m_dataRoot, &QLineEdit::textChanged, this, [this](const QString& t) {
        const QString path = t.isEmpty() ? AppPaths::defaultDataRoot() : t;
        m_dataWarning->setText(AppPaths::onSystemDrive(path) ? tr("<span style='color:#E5A33D'>This is on the system drive; another drive keeps C: free.</span>") : QString());
    });
    m_projectsDir = folderField(page, form, tr("Projects folder"), AppPaths::path(AppPaths::Kind::Projects));
    m_proxiesDir = folderField(page, form, tr("Proxies folder"), AppPaths::path(AppPaths::Kind::Proxies));
    m_autosaveMinutes = new QSpinBox(page);
    m_autosaveMinutes->setObjectName(QStringLiteral("settingsAutosave"));
    m_autosaveMinutes->setRange(0, 60);
    m_autosaveMinutes->setSuffix(tr(" min"));
    m_autosaveMinutes->setSpecialValueText(tr("Off"));
    form->addRow(tr("Autosave every"), m_autosaveMinutes);
    m_backups = new QCheckBox(tr("Keep the previous version when saving a project"), page);
    form->addRow(tr("Backups"), m_backups);
    m_backupKeep = new QSpinBox(page);
    m_backupKeep->setRange(1, 100);
    m_backupKeep->setSuffix(tr(" versions per project"));
    form->addRow(QString(), m_backupKeep);
    connect(m_backups, &QCheckBox::toggled, m_backupKeep, &QWidget::setEnabled);
    v->addStretch(1);
    return page;
}

QWidget* SettingsDialog::previewPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    m_hwDecode = new QCheckBox(tr("Decode video on the graphics card when it can (D3D11VA)"), page);
    form->addRow(tr("Hardware decoding"), m_hwDecode);
    form->addRow(QString(), note(page, tr("Turn off only to rule out graphics driver problems; the CPU then decodes everything. Takes effect after a restart.")));
    m_useProxies = new QCheckBox(tr("Use proxies for preview where they exist"), page);
    form->addRow(tr("Proxies"), m_useProxies);
    form->addRow(QString(), note(page, tr("Make proxies from the Media panel (right-click > Create Proxy) or the Performance panel. Exports always use the original media.")));
    return page;
}

QWidget* SettingsDialog::editingPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    m_transitionSeconds = new QDoubleSpinBox(page);
    m_transitionSeconds->setRange(0.1, 10.0);
    m_transitionSeconds->setSingleStep(0.25);
    m_transitionSeconds->setSuffix(tr(" s"));
    form->addRow(tr("New transitions last"), m_transitionSeconds);
    m_snapDefault = new QCheckBox(tr("Snapping on when LumaForge starts"), page);
    form->addRow(tr("Snapping"), m_snapDefault);
    return page;
}

QWidget* SettingsDialog::exportPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    m_exportsDir = folderField(page, form, tr("Exports folder"), AppPaths::path(AppPaths::Kind::Exports));
    form->addRow(QString(), note(page, tr("Where new exports are suggested. Each export can still be saved anywhere.")));
    return page;
}

QWidget* SettingsDialog::keyboardPage(ActionRegistry& actions) {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(new ShortcutEditor(actions, page));   // changes apply immediately, as in Help > Keyboard Shortcuts
    return page;
}

void SettingsDialog::load() {
    QSettings s;
    m_dataRoot->setText(QDir::toNativeSeparators(s.value(QStringLiteral("paths/dataRoot")).toString()));
    m_projectsDir->setText(QDir::toNativeSeparators(AppPaths::override(AppPaths::Kind::Projects)));
    m_proxiesDir->setText(QDir::toNativeSeparators(AppPaths::override(AppPaths::Kind::Proxies)));
    m_exportsDir->setText(QDir::toNativeSeparators(AppPaths::override(AppPaths::Kind::Exports)));
    m_autosaveMinutes->setValue(m_autosave.intervalMinutes());
    m_backups->setChecked(s.value(QStringLiteral("backups/enabled"), true).toBool());
    m_backupKeep->setValue(s.value(QStringLiteral("backups/keep"), 10).toInt());
    m_backupKeep->setEnabled(m_backups->isChecked());
    m_hwDecode->setChecked(hardwareDecodeAllowed());
    m_useProxies->setChecked(m_proxies.useProxies());
    m_transitionSeconds->setValue(defaultTransitionSeconds());
    m_snapDefault->setChecked(s.value(QStringLiteral("timeline/snapping"), true).toBool());
}

void SettingsDialog::apply() {
    QSettings s;
    bool restart = false;
    const QString root = QDir::fromNativeSeparators(m_dataRoot->text().trimmed());
    if (root != s.value(QStringLiteral("paths/dataRoot")).toString()) {
        AppPaths::setDataRoot(root);
        restart = true;
    }
    AppPaths::setOverride(AppPaths::Kind::Projects, QDir::fromNativeSeparators(m_projectsDir->text().trimmed()));
    AppPaths::setOverride(AppPaths::Kind::Proxies, QDir::fromNativeSeparators(m_proxiesDir->text().trimmed()));
    AppPaths::setOverride(AppPaths::Kind::Exports, QDir::fromNativeSeparators(m_exportsDir->text().trimmed()));
    m_autosave.setIntervalMinutes(m_autosaveMinutes->value());
    s.setValue(QStringLiteral("backups/enabled"), m_backups->isChecked());
    s.setValue(QStringLiteral("backups/keep"), m_backupKeep->value());
    if (m_hwDecode->isChecked() != hardwareDecodeAllowed()) {
        s.setValue(QStringLiteral("preview/hardwareDecode"), m_hwDecode->isChecked());
        restart = true;
    }
    m_proxies.setUseProxies(m_useProxies->isChecked());
    s.setValue(QStringLiteral("editing/transitionSeconds"), m_transitionSeconds->value());
    s.setValue(QStringLiteral("timeline/snapping"), m_snapDefault->isChecked());
    if (restart) emit restartNeeded();
}

}  // namespace lf::ui
