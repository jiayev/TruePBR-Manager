#include "TexturePreviewWidget.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QWheelEvent>

namespace tpbr
{

TexturePreviewWidget::TexturePreviewWidget(QWidget* parent) : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setBackgroundBrush(QBrush(QColor(48, 48, 48)));
}

void TexturePreviewWidget::setImage(const QImage& image)
{
    m_scene->clear();
    m_scene->addPixmap(QPixmap::fromImage(image));
    setSceneRect(m_scene->itemsBoundingRect());
}

void TexturePreviewWidget::clear()
{
    m_scene->clear();
}

void TexturePreviewWidget::wheelEvent(QWheelEvent* event)
{
    const double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    scale(factor, factor);
}

} // namespace tpbr
