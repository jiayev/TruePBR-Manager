#include "TexturePreviewWidget.h"

#include <QEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace tpbr
{

// ─── Zoomable graphics view ────────────────────────────────

class ZoomableView : public QGraphicsView
{
  public:
    using QGraphicsView::QGraphicsView;

  protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
        scale(factor, factor);
    }
};

// ─── TexturePreviewWidget ──────────────────────────────────

TexturePreviewWidget::TexturePreviewWidget(QWidget* parent) : QWidget(parent)
{
    setupUI();
}

void TexturePreviewWidget::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Channel selector bar
    m_channelBar = new QWidget(this);
    auto* barLayout = new QHBoxLayout(m_channelBar);
    barLayout->setContentsMargins(4, 2, 4, 2);
    barLayout->setSpacing(2);

    m_channelGroup = new QButtonGroup(this);
    m_channelGroup->setExclusive(true);

    auto addBtn = [&](const QString& text, PreviewChannel ch, const QString& color)
    {
        auto* btn = new QPushButton(text, m_channelBar);
        btn->setCheckable(true);
        btn->setFixedSize(32, 24);
        btn->setStyleSheet(
            QString("QPushButton { background: #2a2a2a; color: %1; border: 1px solid #555; border-radius: 3px; }"
                    "QPushButton:checked { background: %1; color: #000; }")
                .arg(color));
        m_channelGroup->addButton(btn, static_cast<int>(ch));
        barLayout->addWidget(btn);
    };

    addBtn("All", PreviewChannel::All, "#ccc");
    addBtn("R", PreviewChannel::R, "#ff4444");
    addBtn("G", PreviewChannel::G, "#44ff44");
    addBtn("B", PreviewChannel::B, "#4488ff");
    addBtn("A", PreviewChannel::A, "#cccccc");

    barLayout->addStretch();
    m_channelBar->setVisible(false); // Hidden until an image is loaded
    layout->addWidget(m_channelBar);

    // Default: "All" checked
    if (auto* allBtn = m_channelGroup->button(static_cast<int>(PreviewChannel::All)))
    {
        allBtn->setChecked(true);
    }

    connect(m_channelGroup, &QButtonGroup::idClicked, this, &TexturePreviewWidget::onChannelChanged);

    // Graphics view
    m_scene = new QGraphicsScene(this);
    m_view = new ZoomableView(this);
    m_view->setScene(m_scene);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform);
    m_view->setBackgroundBrush(QBrush(QColor(48, 48, 48)));
    layout->addWidget(m_view, 1);
}

void TexturePreviewWidget::setImage(const QImage& image)
{
    m_sourceImage = image;
    m_currentChannel = PreviewChannel::All;

    // Reset channel button to "All"
    if (auto* allBtn = m_channelGroup->button(static_cast<int>(PreviewChannel::All)))
    {
        allBtn->setChecked(true);
    }

    m_channelBar->setVisible(!image.isNull());
    applyChannelFilter();
}

void TexturePreviewWidget::clear()
{
    m_sourceImage = QImage();
    m_scene->clear();
    m_channelBar->setVisible(false);
}

void TexturePreviewWidget::setChannelSelectorVisible(bool visible)
{
    m_channelBar->setVisible(visible && !m_sourceImage.isNull());
}

void TexturePreviewWidget::onChannelChanged(int id)
{
    m_currentChannel = static_cast<PreviewChannel>(id);
    applyChannelFilter();
}

void TexturePreviewWidget::applyChannelFilter()
{
    if (m_sourceImage.isNull())
    {
        m_scene->clear();
        return;
    }

    QImage display;
    if (m_currentChannel == PreviewChannel::All)
    {
        display = m_sourceImage;
    }
    else
    {
        display = extractChannel(m_sourceImage, m_currentChannel);
    }

    m_scene->clear();
    m_scene->addPixmap(QPixmap::fromImage(display));
    m_view->setSceneRect(m_scene->itemsBoundingRect());
}

QImage TexturePreviewWidget::extractChannel(const QImage& source, PreviewChannel channel)
{
    QImage src = source.convertToFormat(QImage::Format_RGBA8888);
    QImage result(src.width(), src.height(), QImage::Format_RGB888);

    for (int y = 0; y < src.height(); ++y)
    {
        const uint8_t* srcLine = src.constScanLine(y);
        uint8_t* dstLine = result.scanLine(y);

        for (int x = 0; x < src.width(); ++x)
        {
            uint8_t val = 0;
            switch (channel)
            {
            case PreviewChannel::R:
                val = srcLine[x * 4 + 0];
                break;
            case PreviewChannel::G:
                val = srcLine[x * 4 + 1];
                break;
            case PreviewChannel::B:
                val = srcLine[x * 4 + 2];
                break;
            case PreviewChannel::A:
                val = srcLine[x * 4 + 3];
                break;
            default:
                break;
            }

            dstLine[x * 3 + 0] = val;
            dstLine[x * 3 + 1] = val;
            dstLine[x * 3 + 2] = val;
        }
    }

    return result;
}

void TexturePreviewWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
}

} // namespace tpbr
