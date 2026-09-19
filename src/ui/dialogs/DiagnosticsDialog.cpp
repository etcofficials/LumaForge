#include "ui/dialogs/DiagnosticsDialog.h"

#include "lumaforge/core/StringUtil.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }

QTreeWidgetItem* addGroup(QTreeWidget* tree, const QString& title) {
    auto* item = new QTreeWidgetItem(tree, {title});
    QFont f = item->font(0);
    f.setWeight(QFont::DemiBold);
    item->setFont(0, f);
    item->setFirstColumnSpanned(true);
    item->setExpanded(true);
    return item;
}

QTreeWidgetItem* addRow(QTreeWidgetItem* group, const QString& key, const QString& value, std::optional<bool> ok = std::nullopt) {
    auto* item = new QTreeWidgetItem(group, {key, value});
    if (ok.has_value()) item->setForeground(1, *ok ? Theme::palette().success : Theme::palette().textMuted);
    return item;
}

QTreeWidget* makeTree(QWidget* parent) {
    auto* tree = new QTreeWidget(parent);
    tree->setColumnCount(2);
    tree->setHeaderLabels({QObject::tr("Property"), QObject::tr("Value")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    tree->setFocusPolicy(Qt::NoFocus);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setStretchLastSection(true);
    tree->setIndentation(12);
    return tree;
}
}  // namespace

DiagnosticsDialog::DiagnosticsDialog(std::shared_ptr<log::RingBufferSink> logSink, QWidget* parent)
    : QDialog(parent), m_logSink(std::move(logSink)) {
    setWindowTitle(tr("Diagnostics"));
    resize(760, 560);
    setSizeGripEnabled(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildSystemTab(), tr("System"));
    m_tabs->addTab(buildGraphicsTab(), tr("Graphics && Acceleration"));
    m_tabs->addTab(buildLogTab(), tr("Log"));
    layout->addWidget(m_tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* copy = buttons->addButton(tr("Copy Report"), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, this, &DiagnosticsDialog::copyReportToClipboard);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (m_logSink) {
        for (const auto& e : m_logSink->snapshot()) appendLogEntry(e);
        // The sink calls from whichever thread logs; hop to the UI thread.
        m_logSink->setListener([this](const log::Entry& e) {
            QMetaObject::invokeMethod(this, [this, e] { appendLogEntry(e); }, Qt::QueuedConnection);
        });
    }
}

DiagnosticsDialog::~DiagnosticsDialog() {
    if (m_logSink) m_logSink->setListener(nullptr);
}

QWidget* DiagnosticsDialog::buildSystemTab() {
    auto* w = new QWidget(this);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 8, 0, 0);
    m_systemTree = makeTree(w);
    l->addWidget(m_systemTree);
    return w;
}

QWidget* DiagnosticsDialog::buildGraphicsTab() {
    auto* w = new QWidget(this);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 8, 0, 0);
    m_graphicsSummary = new QLabel(tr("Detecting hardware…"), w);
    m_graphicsSummary->setWordWrap(true);
    m_graphicsSummary->setProperty("secondary", true);
    m_graphicsSummary->setContentsMargins(4, 0, 4, 6);
    l->addWidget(m_graphicsSummary);
    m_graphicsTree = makeTree(w);
    l->addWidget(m_graphicsTree, 1);
    return w;
}

QWidget* DiagnosticsDialog::buildLogTab() {
    auto* w = new QWidget(this);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 8, 0, 0);
    m_logView = new QPlainTextEdit(w);
    m_logView->setProperty("log", true);
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_logView->setMaximumBlockCount(5000);
    m_logView->setFont(Theme::monoFont());
    l->addWidget(m_logView);
    return w;
}

void DiagnosticsDialog::appendLogEntry(const log::Entry& entry) {
    m_logView->appendPlainText(qs(log::formatEntry(entry)));
}

void DiagnosticsDialog::setSystemReport(const diag::SystemReport& r, const QString& qtVersion, const QString& previewBackend) {
    m_system = r;
    m_qtVersion = qtVersion;
    m_previewBackend = previewBackend;
    m_systemTree->clear();

    auto* app = addGroup(m_systemTree, tr("LumaForge"));
    addRow(app, tr("Version"), QStringLiteral("%1 (%2)").arg(qs(r.build.appVersion), qs(r.build.buildType)));
    addRow(app, tr("Compiler"), qs(r.build.compiler));
    addRow(app, tr("Qt"), qtVersion);
    addRow(app, tr("FFmpeg"), qs(r.build.ffmpegVersion));
    addRow(app, tr("FFmpeg licence"), qs(r.build.ffmpegLicense));
    addRow(app, tr("Preview backend"), previewBackend);

    auto* os = addGroup(m_systemTree, tr("Operating system"));
    addRow(os, tr("Name"), qs(r.os.name));
    addRow(os, tr("Version"), qs(r.os.version));
    addRow(os, tr("Architecture"), r.os.is64Bit ? QStringLiteral("64-bit") : QStringLiteral("32-bit"));

    auto* cpu = addGroup(m_systemTree, tr("Processor"));
    addRow(cpu, tr("Model"), qs(r.cpu.brand));
    addRow(cpu, tr("Cores / threads"), QStringLiteral("%1 / %2").arg(r.cpu.physicalCores).arg(r.cpu.logicalProcessors));
    QStringList isa;
    if (r.cpu.sse4_2) isa << "SSE4.2";
    if (r.cpu.avx) isa << "AVX";
    if (r.cpu.avx2) isa << "AVX2";
    addRow(cpu, tr("Instruction sets"), isa.join(", "));

    auto* mem = addGroup(m_systemTree, tr("Memory"));
    addRow(mem, tr("Installed"), qs(formatBytes(r.memory.totalPhysical)));
    addRow(mem, tr("Available"), qs(formatBytes(r.memory.availablePhysical)));
    addRow(mem, tr("LumaForge working set"), qs(formatBytes(r.memory.processWorkingSet)));
}

void DiagnosticsDialog::selectTab(const QString& name) {
    const QString n = name.toLower();
    m_tabs->setCurrentIndex(n == QLatin1String("graphics") ? 1 : n == QLatin1String("log") ? 2 : 0);
}

void DiagnosticsDialog::setHardwarePending() {
    m_graphicsSummary->setText(tr("Detecting graphics hardware and encoders…"));
    m_graphicsTree->clear();
}

void DiagnosticsDialog::setHardwareReport(const hw::HwReport& r) {
    m_hardware = r;
    m_graphicsTree->clear();

    // Plain-language verdict first.
    QStringList verdict;
    verdict << (r.d3d11Available ? tr("Direct3D 11 is available (feature level %1); the preview is GPU-rendered.").arg(qs(r.d3d11FeatureLevel))
                                 : tr("Direct3D 11 is not available; the preview uses software rendering."));
    verdict << (r.supportsDecoder("H.264") ? tr("Hardware video decoding is available for H.264.")
                                           : tr("No hardware video decoding; decoding runs on the CPU."));
    if (const auto* hwEnc = r.firstAvailableEncoder(hw::EncoderKind::Hardware)) verdict << tr("Hardware encoding: %1.").arg(qs(hwEnc->displayName));
    else if (const auto* swEnc = r.firstAvailableEncoder(hw::EncoderKind::Software)) verdict << tr("No hardware encoder on this GPU; export will use %1 on the CPU.").arg(qs(swEnc->displayName));
    else verdict << tr("No H.264 encoder is available.");
    m_graphicsSummary->setText(verdict.join(QLatin1Char(' ')));

    for (std::size_t i = 0; i < r.adapters.size(); ++i) {
        const auto& a = r.adapters[i];
        auto* g = addGroup(m_graphicsTree, tr("Adapter %1%2").arg(i).arg(static_cast<int>(i) == r.activeAdapterIndex ? tr(" (active)") : QString()));
        addRow(g, tr("Name"), qs(a.name));
        addRow(g, tr("Vendor"), QStringLiteral("%1 (0x%2)").arg(hw::vendorName(a.vendor)).arg(a.vendorId, 4, 16, QLatin1Char('0')).toUpper());
        addRow(g, tr("Device ID"), QStringLiteral("0x%1  rev %2").arg(a.deviceId, 4, 16, QLatin1Char('0')).arg(a.revision));
        addRow(g, tr("Dedicated VRAM"), qs(formatBytes(a.dedicatedVideoMemory)));
        addRow(g, tr("Shared system memory"), qs(formatBytes(a.sharedSystemMemory)));
        addRow(g, tr("Driver"), a.driverVersion.empty() ? tr("unknown") : qs(a.driverVersion));
        if (a.isSoftware) addRow(g, tr("Type"), tr("Software adapter"));
    }

    auto* d3d = addGroup(m_graphicsTree, tr("Direct3D 11"));
    addRow(d3d, tr("Device"), r.d3d11Available ? tr("Available") : tr("Unavailable: %1").arg(qs(r.d3d11Error)), r.d3d11Available);
    if (r.d3d11Available) {
        addRow(d3d, tr("Feature level"), qs(r.d3d11FeatureLevel));
        addRow(d3d, tr("Video device (D3D11VA)"), r.d3d11VideoDeviceAvailable ? tr("Available") : tr("Unavailable"), r.d3d11VideoDeviceAvailable);
    }

    auto* dec = addGroup(m_graphicsTree, tr("Hardware video decoding (D3D11VA)"));
    for (const auto& p : r.decoderProfiles) addRow(dec, qs(p.codec), p.supported ? tr("Supported") : tr("Not supported"), p.supported);

    auto* enc = addGroup(m_graphicsTree, tr("H.264 encoders (probed by opening each one)"));
    for (const auto& e : r.encoders) {
        const QString kind = e.kind == hw::EncoderKind::Hardware ? tr("hardware") : tr("software");
        addRow(enc, QStringLiteral("%1 (%2)").arg(qs(e.displayName), kind),
               e.available ? tr("Available") : tr("Unavailable: %1").arg(qs(e.detail)), e.available);
    }

    auto* ff = addGroup(m_graphicsTree, tr("FFmpeg hardware device types compiled in"));
    QStringList types;
    for (const auto& t : r.ffmpegHwDeviceTypes) types << qs(t);
    addRow(ff, tr("Types"), types.join(", "));
}

QString DiagnosticsDialog::reportAsText() const {
    QString out;
    auto dump = [&out](QTreeWidget* tree, const QString& title) {
        out += "== " + title + "\n";
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* g = tree->topLevelItem(i);
            out += "[" + g->text(0) + "]\n";
            for (int j = 0; j < g->childCount(); ++j) out += "  " + g->child(j)->text(0) + ": " + g->child(j)->text(1) + "\n";
        }
        out += "\n";
    };
    dump(m_systemTree, QStringLiteral("System"));
    out += "== Graphics summary\n" + m_graphicsSummary->text() + "\n\n";
    dump(m_graphicsTree, QStringLiteral("Graphics & Acceleration"));
    out += "== Log (last entries)\n" + m_logView->toPlainText().right(20000) + "\n";
    return out;
}

void DiagnosticsDialog::copyReportToClipboard() {
    QApplication::clipboard()->setText(reportAsText());
}

}  // namespace lf::ui
