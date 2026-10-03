#pragma once

#include "lumaforge/project/Document.h"
#include "ui/inspector/InspectorSection.h"

#include <QWidget>

#include <vector>

class QLabel;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QVBoxLayout;

namespace lf::ui {

class EmptyState;
class PanelHeader;

// Contextual properties for the timeline selection. A clip-info strip sits
// on top; below it a tab bar (Video, Transform, Color, Effects, Audio, Speed,
// Text, Transition) offers only the tabs that apply to the selection. With
// several items selected only the properties every item has are shown, and
// edits apply to all of them in one undo step.
class InspectorPanel : public QWidget, public InspectorContext {
    Q_OBJECT
public:
    explicit InspectorPanel(project::Document& doc, QWidget* parent = nullptr);
    ~InspectorPanel() override;

    // The panel title strip; MainWindow uses it as the dock title bar.
    [[nodiscard]] PanelHeader* header() const noexcept { return m_header; }

    // InspectorContext
    void editSelection(const QString& undoText, int propertyId, const ClipEdit& fn) override;
    void endGesture() override;
    void pushEdit(const QString& undoText, const EditBuilder& build) override;
    [[nodiscard]] const project::Project& project() const override { return m_doc.project(); }
    [[nodiscard]] TimePoint playhead() const override { return m_playhead; }
    void seekTo(TimePoint t) override { emit seekRequested(t); }

    // Switches to the section's tab, expands and scrolls to it; false when hidden.
    bool revealSection(const QString& key);
    [[nodiscard]] bool hasSection(const QString& key) const;
    // Selects a tab if it is offered for the current selection.
    void showTab(InspectorTab tab);
    [[nodiscard]] static QString tabName(InspectorTab tab);

public slots:
    void setSelection(const std::vector<lf::project::ClipId>& ids);
    void setPlayhead(lf::TimePoint t);

signals:
    void seekRequested(lf::TimePoint t);   // keyframe navigation

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateBannerHeight();
    void addSection(InspectorSection* section);
    void rebuild();
    void reload();
    void applyTabVisibility();
    void onDocumentChange(const project::Change& change);
    // Selected clips grouped into items (an A/V pair is one item).
    [[nodiscard]] std::vector<std::vector<const project::Clip*>> items() const;
    [[nodiscard]] bool sectionApplies(const InspectorSection* s, const std::vector<std::vector<const project::Clip*>>& items) const;

    project::Document& m_doc;
    int m_listenerToken = 0;
    std::vector<project::ClipId> m_selection;
    std::vector<InspectorSection*> m_sections;
    InspectorSection* m_infoSection = nullptr;
    int m_gestureSerial = 0;
    bool m_applying = false;
    TimePoint m_playhead;
    std::vector<bool> m_insideState;   // per selected clip: playhead inside it (keyframe buttons enable)
    InspectorTab m_currentTab = InspectorTab::Transform;
    std::vector<InspectorTab> m_tabOrder;

    PanelHeader* m_header;
    QStackedWidget* m_stack;
    EmptyState* m_empty;
    QWidget* m_content;
    QLabel* m_multiBanner;
    QTabBar* m_tabs;
    QScrollArea* m_scroll;
    QVBoxLayout* m_sectionsLayout;
    QVBoxLayout* m_infoLayout;
};

}  // namespace lf::ui
