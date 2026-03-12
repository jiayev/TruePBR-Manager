#pragma once

#include <QGraphicsView>
#include <QImage>

namespace tpbr
{

/// Widget for displaying and navigating a texture (zoom, pan).
class TexturePreviewWidget : public QGraphicsView
{
    Q_OBJECT

  public:
    explicit TexturePreviewWidget(QWidget* parent = nullptr);

    /// Display an image in the viewer
    void setImage(const QImage& image);

    /// Clear the display
    void clear();

  protected:
    void wheelEvent(QWheelEvent* event) override;

  private:
    QGraphicsScene* m_scene = nullptr;
};

} // namespace tpbr
