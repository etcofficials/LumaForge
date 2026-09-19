#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QToolButton;
class QAction;

namespace lf::ui {

// Thin title strip at the top of every panel: uppercase-ish title on the
// left, optional small tool buttons on the right. Uniform across panels so
// the eye reads them as one system.
class PanelHeader : public QWidget {
    Q_OBJECT
public:
    explicit PanelHeader(const QString& title, QWidget* parent = nullptr);

    QToolButton* addAction(QAction* action);
    void addWidget(QWidget* widget);
    void setTitle(const QString& title);

private:
    QLabel* m_title;
    QHBoxLayout* m_layout;
};

}  // namespace lf::ui
