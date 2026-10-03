#include "ui/inspector/TransformSection.h"

#include "ui/widgets/ParamSlider.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QToolButton>

#include <cmath>

namespace lf::ui {

using namespace lf::project;

namespace {
// Wires a slider to one float on the clip: live edits merge, the gesture end closes the step.
void bindFloat(ParamSlider* s, InspectorContext& ctx, const bool& loading, const QString& text, int propertyId, float Transform2D::*member) {
    QObject::connect(s, &ParamSlider::valueEdited, s, [&ctx, &loading, text, propertyId, member](double v) {
        if (loading) return;
        ctx.editSelection(text, propertyId, [v, member](Clip& c) { c.transform.*member = static_cast<float>(v); });
    });
    QObject::connect(s, &ParamSlider::gestureFinished, s, [&ctx] { ctx.endGesture(); });
}
void bindCrop(ParamSlider* s, InspectorContext& ctx, const bool& loading, const QString& text, int propertyId, float Crop::*member) {
    QObject::connect(s, &ParamSlider::valueEdited, s, [&ctx, &loading, text, propertyId, member](double v) {
        if (loading) return;
        // Percent in the UI, fraction in the model; opposite edges may never overlap.
        ctx.editSelection(text, propertyId, [v, member](Clip& c) {
            c.crop.*member = static_cast<float>(v / 100.0);
            if (c.crop.left + c.crop.right > 0.98f) { if (member == &Crop::left) c.crop.right = 0.98f - c.crop.left; else if (member == &Crop::right) c.crop.left = 0.98f - c.crop.right; }
            if (c.crop.top + c.crop.bottom > 0.98f) { if (member == &Crop::top) c.crop.bottom = 0.98f - c.crop.top; else if (member == &Crop::bottom) c.crop.top = 0.98f - c.crop.bottom; }
        });
    });
    QObject::connect(s, &ParamSlider::gestureFinished, s, [&ctx] { ctx.endGesture(); });
}
// Picture size of a visual clip in source pixels (display orientation).
bool sourceSize(const Project& p, const Clip& c, double& w, double& h) {
    if (c.kind == ClipKind::Text) { w = p.settings.width; h = p.settings.height; return true; }
    const MediaItem* m = p.findMedia(c.media);
    if (!m || !m->info.video || m->info.video->width <= 0 || m->info.video->height <= 0) return false;
    w = m->info.video->width;
    h = m->info.video->height;
    if (std::abs(m->info.video->rotationDegrees) % 180 == 90) std::swap(w, h);
    return true;
}

QToolButton* smallButton(QWidget* parent, const QString& text, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedHeight(22);
    b->setMinimumWidth(30);
    return b;
}

QToolButton* flipButton(QWidget* parent, const QString& text, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedHeight(22);
    return b;
}
}  // namespace

// ---------------------------------------------------------------- transform

TransformSection::TransformSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Transform"), QStringLiteral("transform"), ctx, parent) {
    auto* f = form();
    m_posX = new ParamSlider(-4096, 4096, 0, 1, this);
    m_posX->setSliderRange(-1000, 1000);
    m_posX->setSuffix(QStringLiteral(" px"));
    m_posY = new ParamSlider(-4096, 4096, 0, 1, this);
    m_posY->setSliderRange(-1000, 1000);
    m_posY->setSuffix(QStringLiteral(" px"));
    m_scale = new ParamSlider(0, 1000, 100, 1, this);
    m_scale->setSliderRange(0, 400);
    m_scale->setSuffix(QStringLiteral(" %"));
    m_scaleX = new ParamSlider(0, 1000, 100, 1, this);
    m_scaleX->setSliderRange(0, 400);
    m_scaleX->setSuffix(QStringLiteral(" %"));
    m_scaleY = new ParamSlider(0, 1000, 100, 1, this);
    m_scaleY->setSliderRange(0, 400);
    m_scaleY->setSuffix(QStringLiteral(" %"));
    m_rotation = new ParamSlider(-3600, 3600, 0, 1, this);
    m_rotation->setSliderRange(-180, 180);
    m_rotation->setSuffix(QStringLiteral("°"));
    m_opacity = new ParamSlider(0, 100, 100, 0, this);
    m_opacity->setSuffix(QStringLiteral(" %"));
    m_uniform = new QCheckBox(tr("Uniform scale"), this);
    m_uniform->setChecked(true);
    m_uniform->setFocusPolicy(Qt::NoFocus);

    f->addRow(tr("Position X"), m_posX);
    f->addRow(tr("Position Y"), m_posY);
    f->addRow(QString(), m_uniform);
    f->addRow(tr("Scale"), m_scale);
    f->addRow(tr("Scale X"), m_scaleX);
    f->addRow(tr("Scale Y"), m_scaleY);
    f->addRow(tr("Rotation"), m_rotation);
    f->addRow(tr("Opacity"), m_opacity);

    auto* flips = new QWidget(this);
    auto* flipRow = new QHBoxLayout(flips);
    flipRow->setContentsMargins(0, 0, 0, 0);
    flipRow->setSpacing(6);
    m_flipH = flipButton(flips, tr("Flip H"), tr("Mirror left / right"));
    m_flipV = flipButton(flips, tr("Flip V"), tr("Mirror top / bottom"));
    flipRow->addWidget(m_flipH);
    flipRow->addWidget(m_flipV);
    flipRow->addStretch(1);
    f->addRow(tr("Flip"), flips);

    // Size presets relative to the frame, and picture-in-picture corners.
    auto* fitRow = new QWidget(this);
    auto* fitLayout = new QHBoxLayout(fitRow);
    fitLayout->setContentsMargins(0, 0, 0, 0);
    fitLayout->setSpacing(4);
    auto* fitB = smallButton(fitRow, tr("Fit"), tr("Fit: whole picture inside the frame (scale 100%)"));
    auto* fillB = smallButton(fitRow, tr("Fill"), tr("Fill: cover the whole frame, cropping the overflow"));
    auto* origB = smallButton(fitRow, tr("1:1"), tr("Original size: one source pixel per frame pixel"));
    for (auto* b : {fitB, fillB, origB}) fitLayout->addWidget(b);
    fitLayout->addStretch(1);
    f->addRow(tr("Size"), fitRow);
    connect(fitB, &QToolButton::clicked, this, [this] { applyFit(Fit::Fit); });
    connect(fillB, &QToolButton::clicked, this, [this] { applyFit(Fit::Fill); });
    connect(origB, &QToolButton::clicked, this, [this] { applyFit(Fit::Original); });

    auto* pipRow = new QWidget(this);
    auto* pipLayout = new QHBoxLayout(pipRow);
    pipLayout->setContentsMargins(0, 0, 0, 0);
    pipLayout->setSpacing(4);
    const std::pair<Corner, std::pair<const char*, const char*>> corners[] = {
        {Corner::TopLeft, {"↖", "Picture-in-picture: top left"}}, {Corner::TopRight, {"↗", "Picture-in-picture: top right"}},
        {Corner::BottomLeft, {"↙", "Picture-in-picture: bottom left"}}, {Corner::BottomRight, {"↘", "Picture-in-picture: bottom right"}},
        {Corner::Center, {"◎", "Picture-in-picture: centred"}}};
    for (const auto& [corner, label] : corners) {
        auto* b = smallButton(pipRow, QString::fromUtf8(label.first), tr(label.second) + tr(" (35% size, 4% margin)"));
        const Corner cc = corner;
        connect(b, &QToolButton::clicked, this, [this, cc] { applyPip(cc); });
        pipLayout->addWidget(b);
    }
    pipLayout->addStretch(1);
    f->addRow(tr("PiP"), pipRow);

    bindFloat(m_posX, m_ctx, m_loading, tr("Position"), prop::PositionX, &Transform2D::positionX);
    bindFloat(m_posY, m_ctx, m_loading, tr("Position"), prop::PositionY, &Transform2D::positionY);
    bindFloat(m_rotation, m_ctx, m_loading, tr("Rotation"), prop::Rotation, &Transform2D::rotation);

    connect(m_scale, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        const float s = static_cast<float>(v / 100.0);
        m_ctx.editSelection(tr("Scale"), prop::ScaleUniform, [s](Clip& c) { c.transform.scaleX = c.transform.scaleY = s; });
        LoadGuard g(m_loading);
        m_scaleX->setValue(v);
        m_scaleY->setValue(v);
    });
    connect(m_scale, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    connect(m_scaleX, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        const float s = static_cast<float>(v / 100.0);
        m_ctx.editSelection(tr("Scale X"), prop::ScaleX, [s](Clip& c) { c.transform.scaleX = s; });
    });
    connect(m_scaleX, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    connect(m_scaleY, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        const float s = static_cast<float>(v / 100.0);
        m_ctx.editSelection(tr("Scale Y"), prop::ScaleY, [s](Clip& c) { c.transform.scaleY = s; });
    });
    connect(m_scaleY, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    connect(m_opacity, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        const float o = static_cast<float>(v / 100.0);
        m_ctx.editSelection(tr("Opacity"), prop::Opacity, [o](Clip& c) { c.transform.opacity = o; });
    });
    connect(m_opacity, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    connect(m_uniform, &QCheckBox::toggled, this, [this](bool on) {
        m_scale->setVisible(on);
        m_scaleX->setVisible(!on);
        m_scaleY->setVisible(!on);
        if (auto* f2 = form()) {
            f2->labelForField(m_scale)->setVisible(on);
            f2->labelForField(m_scaleX)->setVisible(!on);
            f2->labelForField(m_scaleY)->setVisible(!on);
        }
        if (on && !m_loading) {
            // Switching back to uniform adopts the X scale for both axes.
            const float s = static_cast<float>(m_scaleX->value() / 100.0);
            m_ctx.editSelection(tr("Scale"), prop::ScaleUniform, [s](Clip& c) { c.transform.scaleX = c.transform.scaleY = s; });
            m_ctx.endGesture();
        }
    });
    connect(m_flipH, &QToolButton::toggled, this, [this](bool on) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Flip Horizontal"), prop::FlipH, [on](Clip& c) { c.transform.flipH = on; });
        m_ctx.endGesture();
    });
    connect(m_flipV, &QToolButton::toggled, this, [this](bool on) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Flip Vertical"), prop::FlipV, [on](Clip& c) { c.transform.flipV = on; });
        m_ctx.endGesture();
    });

    auto* reset = addHeaderButton(tr("Reset"), tr("Reset position, scale, rotation, opacity and flips"));
    connect(reset, &QToolButton::clicked, this, [this] {
        m_ctx.editSelection(tr("Reset Transform"), prop::ResetGroup, [](Clip& c) { c.transform = Transform2D{}; });
        m_ctx.endGesture();
    });
    emit m_uniform->toggled(true);
}

void TransformSection::applyFit(Fit fit) {
    const Project& p = m_ctx.project();
    const QString text = fit == Fit::Fit ? tr("Fit to Frame") : fit == Fit::Fill ? tr("Fill Frame") : tr("Original Size");
    m_ctx.editSelection(text, prop::Preset, [&p, fit](Clip& c) {
        double w = 0, h = 0;
        if (!sourceSize(p, c, w, h)) return;
        const double fw = p.settings.width, fh = p.settings.height;
        const double base = std::min(fw / w, fh / h);   // the "fit" scale the compositor applies
        double s = 1.0;
        if (fit == Fit::Fill) s = std::max(fw / w, fh / h) / base;
        else if (fit == Fit::Original) s = 1.0 / base;
        c.transform.scaleX = c.transform.scaleY = static_cast<float>(s);
        c.transform.positionX = c.transform.positionY = 0.0f;
    });
    m_ctx.endGesture();
}

void TransformSection::applyPip(Corner corner) {
    const Project& p = m_ctx.project();
    m_ctx.editSelection(tr("Picture in Picture"), prop::Preset, [&p, corner](Clip& c) {
        double w = 0, h = 0;
        if (!sourceSize(p, c, w, h)) return;
        const double fw = p.settings.width, fh = p.settings.height;
        const double base = std::min(fw / w, fh / h);
        constexpr double kScale = 0.35;
        const double dispW = w * base * kScale, dispH = h * base * kScale;
        const double margin = fw * 0.04;
        const double dx = fw / 2 - margin - dispW / 2, dy = fh / 2 - margin - dispH / 2;
        double x = 0, y = 0;
        switch (corner) {
            case Corner::TopLeft: x = -dx; y = -dy; break;
            case Corner::TopRight: x = dx; y = -dy; break;
            case Corner::BottomLeft: x = -dx; y = dy; break;
            case Corner::BottomRight: x = dx; y = dy; break;
            case Corner::Center: break;
        }
        c.transform.scaleX = c.transform.scaleY = static_cast<float>(kScale);
        c.transform.positionX = static_cast<float>(x);
        c.transform.positionY = static_cast<float>(y);
    });
    m_ctx.endGesture();
}

void TransformSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    const auto& t = c.transform;
    m_posX->setValue(t.positionX);
    m_posY->setValue(t.positionY);
    m_scaleX->setValue(t.scaleX * 100.0);
    m_scaleY->setValue(t.scaleY * 100.0);
    m_scale->setValue(t.scaleX * 100.0);
    const bool uniform = std::fabs(t.scaleX - t.scaleY) < 1e-4f;
    if (m_uniform->isChecked() != uniform) m_uniform->setChecked(uniform);
    m_rotation->setValue(t.rotation);
    m_opacity->setValue(std::round(t.opacity * 100.0));
    m_flipH->setChecked(t.flipH);
    m_flipV->setChecked(t.flipV);
}

// ---------------------------------------------------------------- crop

CropSection::CropSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Crop"), QStringLiteral("crop"), ctx, parent) {
    auto* f = form();
    auto make = [&] {
        auto* s = new ParamSlider(0, 98, 0, 1, this);
        s->setSuffix(QStringLiteral(" %"));
        return s;
    };
    m_left = make();
    m_right = make();
    m_top = make();
    m_bottom = make();
    f->addRow(tr("Left"), m_left);
    f->addRow(tr("Right"), m_right);
    f->addRow(tr("Top"), m_top);
    f->addRow(tr("Bottom"), m_bottom);
    bindCrop(m_left, m_ctx, m_loading, tr("Crop"), prop::CropLeft, &Crop::left);
    bindCrop(m_right, m_ctx, m_loading, tr("Crop"), prop::CropRight, &Crop::right);
    bindCrop(m_top, m_ctx, m_loading, tr("Crop"), prop::CropTop, &Crop::top);
    bindCrop(m_bottom, m_ctx, m_loading, tr("Crop"), prop::CropBottom, &Crop::bottom);
    m_feather = new ParamSlider(0, 200, 0, 0, this);
    m_feather->setSuffix(QStringLiteral(" px"));
    m_feather->setToolTip(tr("Soft edge inside the crop, in source pixels"));
    f->addRow(tr("Feather"), m_feather);
    connect(m_feather, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Crop Feather"), prop::CropFeather, [v](Clip& c) { c.crop.feather = static_cast<float>(v); });
    });
    connect(m_feather, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    auto* reset = addHeaderButton(tr("Reset"), tr("Remove the crop"));
    connect(reset, &QToolButton::clicked, this, [this] {
        m_ctx.editSelection(tr("Reset Crop"), prop::ResetGroup, [](Clip& c) { c.crop = Crop{}; });
        m_ctx.endGesture();
    });
}

void CropSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    m_left->setValue(c.crop.left * 100.0);
    m_right->setValue(c.crop.right * 100.0);
    m_top->setValue(c.crop.top * 100.0);
    m_bottom->setValue(c.crop.bottom * 100.0);
    m_feather->setValue(c.crop.feather);
}

}  // namespace lf::ui
