#include "ui/panels/InspectorPanel.h"

#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"
#include "ui/inspector/AudioSection.h"
#include "ui/inspector/ClipInfoSection.h"
#include "ui/inspector/ColorSection.h"
#include "ui/inspector/EffectsSection.h"
#include "ui/inspector/TextSections.h"
#include "ui/inspector/TransitionSection.h"
#include "ui/inspector/MaskSection.h"
#include "ui/inspector/GeneratedSections.h"
#include "ui/inspector/TransformSection.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/PanelHeader.h"

#include <QLabel>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

#include <algorithm>

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

    m_content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    m_multiBanner = new QLabel(m_content);
    m_multiBanner->setObjectName(QStringLiteral("inspectorMultiBanner"));
    m_multiBanner->setWordWrap(true);
    m_multiBanner->setContentsMargins(10, 6, 10, 6);
    m_multiBanner->hide();
    contentLayout->addWidget(m_multiBanner);
    auto* infoHost = new QWidget(m_content);
    m_infoLayout = new QVBoxLayout(infoHost);
    m_infoLayout->setContentsMargins(0, 0, 0, 0);
    m_infoLayout->setSpacing(0);
    contentLayout->addWidget(infoHost);

    m_tabs = new QTabBar(m_content);
    m_tabs->setObjectName(QStringLiteral("inspectorTabs"));
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideNone);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_tabOrder = {InspectorTab::Video, InspectorTab::Transform, InspectorTab::Color, InspectorTab::Effects,
                  InspectorTab::Audio, InspectorTab::Speed, InspectorTab::Text, InspectorTab::Transition};
    for (InspectorTab t : m_tabOrder) {
        const int i = m_tabs->addTab(tabName(t));
        m_tabs->setTabData(i, static_cast<int>(t));
    }
    contentLayout->addWidget(m_tabs);
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) {
        if (i < 0) return;
        m_currentTab = static_cast<InspectorTab>(m_tabs->tabData(i).toInt());
        QSettings().setValue(QStringLiteral("inspector/tab"), static_cast<int>(m_currentTab));
        applyTabVisibility();
    });

    m_scroll = new QScrollArea(m_content);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* host = new QWidget(m_scroll);
    m_sectionsLayout = new QVBoxLayout(host);
    m_sectionsLayout->setContentsMargins(0, 0, 0, 12);
    m_sectionsLayout->setSpacing(0);
    m_sectionsLayout->addStretch(1);
    m_scroll->setWidget(host);
    contentLayout->addWidget(m_scroll, 1);
    m_stack->addWidget(m_content);
    m_stack->setCurrentWidget(m_empty);
    layout->addWidget(m_stack, 1);

    m_infoSection = new ClipInfoSection(*this, infoHost);
    m_infoLayout->addWidget(m_infoSection);
    addSection(new TextContentSection(*this, host));
    addSection(new TypographySection(*this, host));
    addSection(new TextStyleSection(TextStyleSection::Part::Fill, *this, host));
    addSection(new TextStyleSection(TextStyleSection::Part::Outline, *this, host));
    addSection(new TextStyleSection(TextStyleSection::Part::Shadow, *this, host));
    addSection(new TextStyleSection(TextStyleSection::Part::Background, *this, host));
    addSection(new TransformSection(*this, host));
    addSection(new SolidSection(*this, host));
    addSection(new AdjustmentSection(*this, host));
    addSection(new CropSection(*this, host));
    addSection(new MaskSection(*this, host));
    addSection(new ColorSection(*this, host));
    addSection(new EffectsSection(*this, host));
    addSection(new AudioSection(*this, host));
    addSection(new SpeedSection(*this, host));
    addSection(new TransitionSection(*this, host));

    m_currentTab = static_cast<InspectorTab>(QSettings().value(QStringLiteral("inspector/tab"), static_cast<int>(InspectorTab::Transform)).toInt());
    m_listenerToken = m_doc.addListener([this](const Change& c) { onDocumentChange(c); });
}

InspectorPanel::~InspectorPanel() { m_doc.removeListener(m_listenerToken); }

QString InspectorPanel::tabName(InspectorTab tab) {
    switch (tab) {
        case InspectorTab::Video: return tr("Video");
        case InspectorTab::Transform: return tr("Transform");
        case InspectorTab::Color: return tr("Color");
        case InspectorTab::Effects: return tr("Effects");
        case InspectorTab::Audio: return tr("Audio");
        case InspectorTab::Speed: return tr("Speed");
        case InspectorTab::Text: return tr("Text");
        case InspectorTab::Transition: return tr("Transition");
    }
    return {};
}

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

void InspectorPanel::setPlayhead(TimePoint t) {
    if (t == m_playhead) return;
    m_playhead = t;
    // Values shown follow the playhead only for animated clips; keyframe
    // diamonds also change when the playhead enters or leaves a clip.
    const auto& seq = m_doc.project().sequence;
    bool animated = false;
    std::vector<bool> inside;
    for (auto id : m_selection) {
        if (const Clip* c = seq.findClip(id)) {
            animated |= !c->animation.empty();
            inside.push_back(c->contains(t));
        }
    }
    if (animated || inside != m_insideState) {
        m_insideState = std::move(inside);
        reload();
    }
}

std::vector<std::vector<const Clip*>> InspectorPanel::items() const {
    const auto& seq = m_doc.project().sequence;
    std::vector<std::vector<const Clip*>> out;
    std::vector<ClipId> seen;
    for (auto id : m_selection) {
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        const Clip* c = seq.findClip(id);
        if (!c) continue;
        std::vector<const Clip*> item{c};
        seen.push_back(id);
        // A selected linked partner belongs to the same item.
        if (isValid(c->link) && std::find(m_selection.begin(), m_selection.end(), c->link) != m_selection.end()) {
            if (const Clip* p = seq.findClip(c->link)) { item.push_back(p); seen.push_back(p->id); }
        }
        out.push_back(std::move(item));
    }
    return out;
}

bool InspectorPanel::sectionApplies(const InspectorSection* s, const std::vector<std::vector<const Clip*>>& its) const {
    if (its.empty()) return false;
    // Shown only when every selected item has a clip the section edits.
    for (const auto& item : its) {
        const bool any = std::any_of(item.begin(), item.end(), [&](const Clip* c) { return s->appliesTo(c->kind); });
        if (!any) return false;
    }
    return true;
}

void InspectorPanel::rebuild() {
    const auto its = items();
    if (its.empty()) {
        m_stack->setCurrentWidget(m_empty);
        for (auto* s : m_sections) s->hide();
        return;
    }
    m_stack->setCurrentWidget(m_content);
    const int count = static_cast<int>(its.size());
    m_multiBanner->setVisible(count > 1);
    if (count > 1) {
        m_multiBanner->setText(tr("<b>%n items selected</b> — only properties they all share are shown; changes apply to all of them.", nullptr, count));
        updateBannerHeight();
    }
    // Clip info: the primary clip (first selected).
    m_infoSection->load(*its.front().front(), count);
    for (auto* s : m_sections) {
        const bool applies = sectionApplies(s, its);
        s->setProperty("lfApplies", applies);
        if (!applies) continue;
        for (const auto& item : its)
            for (const Clip* c : item)
                if (s->appliesTo(c->kind)) { s->load(*c, count); goto loaded; }
    loaded:;
    }
    applyTabVisibility();
}

void InspectorPanel::applyTabVisibility() {
    // Offer only the tabs that have at least one applicable section.
    std::vector<InspectorTab> offered;
    for (InspectorTab t : m_tabOrder) {
        const bool any = std::any_of(m_sections.begin(), m_sections.end(), [&](InspectorSection* s) { return s->tab() == t && s->property("lfApplies").toBool(); });
        if (any) offered.push_back(t);
    }
    const QSignalBlocker block(m_tabs);
    // Rebuild the tab set rather than hiding tabs: QTabBar leaves a tab that is
    // re-shown while the panel is hidden with an empty rectangle.
    bool same = m_tabs->count() == static_cast<int>(offered.size());
    for (int i = 0; same && i < m_tabs->count(); ++i) same = static_cast<InspectorTab>(m_tabs->tabData(i).toInt()) == offered[static_cast<std::size_t>(i)];
    if (!same) {
        while (m_tabs->count()) m_tabs->removeTab(0);
        for (InspectorTab t : offered) m_tabs->setTabData(m_tabs->addTab(tabName(t)), static_cast<int>(t));
    }
    if (!offered.empty() && std::find(offered.begin(), offered.end(), m_currentTab) == offered.end()) {
        // Keep the user's tab when possible; otherwise the first useful one (Text for titles, Audio for sound...).
        m_currentTab = offered.front();
        if (std::find(offered.begin(), offered.end(), InspectorTab::Transform) != offered.end() && offered.front() == InspectorTab::Video) m_currentTab = InspectorTab::Transform;
    }
    for (int i = 0; i < m_tabs->count(); ++i) if (static_cast<InspectorTab>(m_tabs->tabData(i).toInt()) == m_currentTab) m_tabs->setCurrentIndex(i);
    m_tabs->setVisible(offered.size() > 1);
    for (auto* s : m_sections) s->setVisible(s->property("lfApplies").toBool() && s->tab() == m_currentTab);
}

void InspectorPanel::updateBannerHeight() {
    // The stacked widget above does not pass height-for-width down, so a
    // wrapped label would be squeezed to one line: size it explicitly.
    if (m_multiBanner->isVisible() || !m_multiBanner->text().isEmpty()) {
        const int w = std::max(100, m_content->width());
        m_multiBanner->setFixedHeight(m_multiBanner->heightForWidth(w));
    }
}

void InspectorPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    updateBannerHeight();
}

void InspectorPanel::showTab(InspectorTab tab) {
    m_currentTab = tab;
    applyTabVisibility();
}

void InspectorPanel::reload() {
    if (m_applying) return;   // our own edit: widgets already show the value
    const auto its = items();
    if (its.empty()) { rebuild(); return; }
    const int count = static_cast<int>(its.size());
    m_infoSection->load(*its.front().front(), count);
    for (auto* s : m_sections) {
        if (!s->property("lfApplies").toBool()) continue;
        for (const auto& item : its)
            for (const Clip* c : item)
                if (s->appliesTo(c->kind)) { s->load(*c, count); goto done; }
    done:;
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
        case Change::Kind::MediaOrganized:
        case Change::Kind::Settings:
        case Change::Kind::TracksChanged:
            reload();
            break;
        default:
            break;
    }
}

bool InspectorPanel::hasSection(const QString& key) const {
    for (auto* s : m_sections) if (s->sectionKey() == key && s->property("lfApplies").toBool()) return true;
    return key == QStringLiteral("clip") && !m_selection.empty();
}

bool InspectorPanel::revealSection(const QString& key) {
    if (key == QStringLiteral("clip")) {
        m_infoSection->setExpanded(true);
        return !m_selection.empty();
    }
    for (auto* s : m_sections) {
        if (s->sectionKey() != key || !s->property("lfApplies").toBool()) continue;
        showTab(s->tab());
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
