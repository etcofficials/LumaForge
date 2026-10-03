#pragma once

#include "lumaforge/project/Document.h"
#include "ui/inspector/InspectorSection.h"

#include <QWidget>

#include <vector>

class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace lf::ui {

class EmptyState;
class PanelHeader;

// Contextual properties for the timeline selection. Sections are created
// once and shown according to the kind of the primary selected clip; edits
// go through InspectorContext so every change is one undoable command.
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

public slots:
    void setSelection(const std::vector<lf::project::ClipId>& ids);

private:
    void addSection(InspectorSection* section);
    void rebuild();
    void reload();
    void onDocumentChange(const project::Change& change);

    project::Document& m_doc;
    int m_listenerToken = 0;
    std::vector<project::ClipId> m_selection;
    std::vector<InspectorSection*> m_sections;
    int m_gestureSerial = 0;
    bool m_applying = false;

    PanelHeader* m_header;
    QStackedWidget* m_stack;
    EmptyState* m_empty;
    QScrollArea* m_scroll;
    QVBoxLayout* m_sectionsLayout;
};

}  // namespace lf::ui
