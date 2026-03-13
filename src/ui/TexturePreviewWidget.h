#pragma once

#include <QButtonGroup>
#include <QGraphicsView>
#include <QImage>
#include <QPushButton>
#include <QWidget>

namespace tpbr
{

/// Channel filter mode for texture preview.
enum class PreviewChannel
{
    All,  // Show full RGBA
    R,    // Red channel as greyscale
    G,    // Green channel as greyscale
    B,    // Blue channel as greyscale
    A,    // Alpha channel as greyscale
};

/// Widget for displaying and navigating a texture (zoom, pan)
/// with optional per-channel filtering.
class TexturePreviewWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit TexturePreviewWidget(QWidget* parent = nullptr);

    /// Display an image in the viewer
    void setImage(const QImage& image);

    /// Clear the display
    void clear();

    /// Show/hide the channel selector buttons
    void setChannelSelectorVisible(bool visible);

  private slots:
    void onChannelChanged(int id);

  private:
    void setupUI();
    void applyChannelFilter();

    /// Extract a single channel from the source image as greyscale
    static QImage extractChannel(const QImage& source, PreviewChannel channel);

    QGraphicsView*  m_view = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QWidget*        m_channelBar = nullptr;
    QButtonGroup*   m_channelGroup = nullptr;

    QImage          m_sourceImage;       // Original full RGBA image
    PreviewChannel  m_currentChannel = PreviewChannel::All;
};

} // namespace tpbr
