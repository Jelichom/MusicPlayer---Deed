#include "clickableslider.h"

#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

ClickableSlider::ClickableSlider(Qt::Orientation orientation, QWidget *parent)
    : QSlider(orientation, parent)
{
}

int ClickableSlider::pixelPosToRangeValue(int pos) const
{
    QStyleOptionSlider option;
    initStyleOption(&option);

    const QRect groove = style()->subControlRect(
        QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(
        QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);

    int sliderMin = 0;
    int sliderMax = 0;
    int sliderLength = 0;

    if (orientation() == Qt::Horizontal) {
        sliderLength = handle.width();
        sliderMin = groove.x();
        sliderMax = groove.right() - sliderLength + 1;
    } else {
        sliderLength = handle.height();
        sliderMin = groove.y();
        sliderMax = groove.bottom() - sliderLength + 1;
    }

    const int halfHandleLength = sliderLength / 2;
    const int positionFromHandleOrigin = pos - sliderMin - halfHandleLength;

    return QStyle::sliderValueFromPosition(
        minimum(),
        maximum(),
        positionFromHandleOrigin,
        sliderMax - sliderMin,
        option.upsideDown
    );
}

void ClickableSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QSlider::mousePressEvent(event);
        return;
    }

    QStyleOptionSlider option;
    initStyleOption(&option);

    const QRect handleRect = style()->subControlRect(
        QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);

    const QPoint point = event->position().toPoint();

    // Normal Qt dragging if the user clicked the handle itself.
    if (handleRect.contains(point)) {
        m_draggingHandle = true;
        QSlider::mousePressEvent(event);
        return;
    }

    // Click on groove: jump there and allow dragging from there.
    m_draggingHandle = false;

    const int pos = (orientation() == Qt::Horizontal) ? point.x() : point.y();
    const int value = pixelPosToRangeValue(pos);

    setSliderDown(true);
    setSliderPosition(value);

    if (hasTracking()) {
        setValue(value);
    }

    event->accept();
}

void ClickableSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        QSlider::mouseMoveEvent(event);
        return;
    }

    if (m_draggingHandle) {
        QSlider::mouseMoveEvent(event);
        return;
    }

    const int pos = (orientation() == Qt::Horizontal)
        ? event->position().toPoint().x()
        : event->position().toPoint().y();

    const int value = pixelPosToRangeValue(pos);

    setSliderPosition(value);

    if (hasTracking()) {
        setValue(value);
    }

    event->accept();
}

void ClickableSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QSlider::mouseReleaseEvent(event);
        return;
    }

    if (m_draggingHandle) {
        m_draggingHandle = false;
        QSlider::mouseReleaseEvent(event);
        return;
    }

    const int pos = (orientation() == Qt::Horizontal)
        ? event->position().toPoint().x()
        : event->position().toPoint().y();

    const int value = pixelPosToRangeValue(pos);

    setSliderPosition(value);
    setValue(value);
    setSliderDown(false);

    event->accept();
}