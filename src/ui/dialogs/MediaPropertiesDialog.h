#pragma once

#include "lumaforge/project/MediaItem.h"

#include <QDialog>
#include <QImage>

namespace lf::ui {

// Read-only metadata sheet for one media item.
class MediaPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    MediaPropertiesDialog(const project::MediaItem& item, const QImage& thumbnail, QWidget* parent = nullptr);
};

}  // namespace lf::ui
