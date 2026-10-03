#include "ui/panels/InspectorPanel.h"

#include "lumaforge/project/TimelineCommands.h"
#include "ui/inspector/ClipInfoSection.h"
#include "ui/inspector/TransformSection.h"
#include "ui/inspector/ColorSection.h"
#include "ui/inspector/EffectsSection.h"
#include "ui/inspector/TextSections.h"
#include "ui/inspector/AudioSection.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/PanelHeader.h"

#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace lf::ui {

using namespace lf::project;

InspectorPanel::InspectorPanel(Document& doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(300);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new PanelHeader(tr("Inspector"), this);
    layout->addWidget(m_header);

    m_stack = new QStackedWidget(this);
    m_empty = new EmptyState(QStringLiteral("inspector-empty"), tr("Nothing selected"),
                             tr("Select a clip in the timeline to edit its properties."), this);
    m_stack->addWidget(m_empty);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* host = new QWidget(m_scroll);
    m_sectionsLayout = new QVBoxLayout(host);
    m_sectionsLayout->setContentsMargins(0, 0, 0, 12);
    m_sectionsLayout->setSpacing(0);
    m_sectionsLayout->addStretch(1);
    m_scroll->setWidget(host);
    m_stack->addWidget(m_scroll);
    m_stack->setCurrentWidget(m_empty);
    layout->addWidget(m_stack, 1);

    addSection(new ClipInfoSection(*this, host));
    addSection(new TextContentSection(*this, host));
    addSection(new TypographySection(*this, host));
    addSection(new TextStyleSection(TextStyleSection::Part::Fill, *this, host));
    addSection(new TextStyleSection(TextStyleSection::Part::Outline, *this, host));
    addSection(new TextStyleSection(TextStyleSection::Part::Shadow, *this, host));
    addSection(new TextStyleSection(TextStyleSection::Part::Background, *this, host));
    addSection(new TransformSection(*this, host));
    addSection(new CropSection(*this, host));
    addSection(new ColorSection(*this, host));
    addSection(new EffectsSection(*this, host));
    addSection(new AudioSection(*this, host));
    addSection(new SpeedSection(*this, host));

    m_listenerToken = m_doc.addListener([this](const Change& c) { onDocumentChange(c); });
}

InspectorPanel::~InspectorPanel() { m_doc.removeListener(m_listenerToken); }

void InspectorPanel::addSection(InspectorSection* section) {
    m_sections.push_back(section);
    m_sectionsLayout->insertWidget(m_sectionsLayout->count() - 1, section);
    section->hide();
}

// ---------------------------------------------------------------- selection / refresh

void InspectorPanel::setSelection(const std::vector<ClipId>& ids) {
    m_selection = ids;
    ++m_gestureSerial;
    rebuild();
}

void InspectorPanel::rebuild() {
    const auto& seq = m_doc.project().sequence;
    const Clip* primary = nullptr;
    int count = 0;
    for (auto id : m_selection) if (const Clip* c = seq.findClip(id)) { if (!primary) primary = c; ++count; }
    if (!primary) {
        m_stack->setCurrentWidget(m_empty);
        for (auto* s : m_sections) s->hide();
        return;
    }
    m_stack->setCurrentWidget(m_scroll);
    for (auto* s : m_sections) {
        // Shown when any selected clip is of a kind the section handles (a
        // video clip with its linked audio selected gets the Audio section).
        const Clip* target = nullptr;
        for (auto id : m_selection) if (const Clip* c = seq.findClip(id); c && s->appliesTo(c->kind)) { target = c; break; }
        s->setVisible(target != nullptr);
        if (target) s->load(*target, count);
    }
}

void InspectorPanel::reload() {
    if (m_applying) return;   // our own edit: widgets already show the value
    const auto& seq = m_doc.project().sequence;
    const Clip* primary = nullptr;
    int count = 0;
    for (auto id : m_selection) if (const Clip* c = seq.findClip(id)) { if (!primary) primary = c; ++count; }
    if (!primary) { rebuild(); return; }
    for (auto* s : m_sections) {
        if (!s->isVisible()) continue;
        for (auto id : m_selection) if (const Clip* c = seq.findClip(id); c && s->appliesTo(c->kind)) { s->load(*c, count); break; }
    }
}

void InspectorPanel::onDocumentChange(const Change& change) {
    switch (change.kind) {
        case Change::Kind::Reset:
            m_selection.clear();
            rebuild();
            break;
        case Change::Kind::ClipsChanged:
        case Change::Kind::MediaChanged:
        case Change::Kind::Settings:
        case Change::Kind::TracksChanged:
            reload();
            break;
        default:
            break;
    }
}

bool InspectorPanel::hasSection(const QString& key) const {
    for (auto* s : m_sections) if (s->sectionKey() == key && !s->isHidden()) return true;
    return false;
}

bool InspectorPanel::revealSection(const QString& key) {
    for (auto* s : m_sections) {
        if (s->sectionKey() != key || s->isHidden()) continue;
        s->setExpanded(true);
        m_scroll->ensureWidgetVisible(s, 0, 0);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- editing

void InspectorPanel::editSelection(const QString& undoText, int propertyId, const ClipEdit& fn) {
    const auto& seq = m_doc.project().sequence;
    SequenceEdit edit;
    for (auto id : m_selection) {
        const Clip* c = seq.findClip(id);
        if (!c) continue;
        if (const Track* t = seq.findTrack(c->track); !t || t->locked) continue;
        Clip n = *c;
        fn(n);
        if (n == *c) continue;
        edit.modify(std::move(n));
    }
    if (edit.empty()) return;
    // Merge id = property + gesture serial: consecutive edits of one control
    // in one gesture collapse; a new gesture (or another control) starts a new step.
    const int mergeId = propertyId * 10000 + (m_gestureSerial % 10000);
    m_applying = true;
    m_doc.push(std::make_unique<SequenceEditCommand>(m_doc, undoText.toStdString(), std::move(edit), mergeId));
    m_applying = false;
}

void InspectorPanel::pushEdit(const QString& undoText, const EditBuilder& build) {
    SequenceEdit edit = build(m_doc, m_selection);
    if (edit.empty()) return;
    ++m_gestureSerial;
    m_doc.push(std::make_unique<SequenceEditCommand>(m_doc, undoText.toStdString(), std::move(edit)));
}

void InspectorPanel::endGesture() {
    ++m_gestureSerial;
    reload();   // widgets show the model (e.g. clamped values) once the gesture is over
}

}  // namespace lf::ui
