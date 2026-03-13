#include "MaterialPreviewWidget.h"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace tpbr
{

MaterialPreviewWidget::MaterialPreviewWidget(QWidget* parent) : QWidget(parent)
{
    // Critical: this widget paints via D3D12 directly to the HWND
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMinimumSize(200, 200);

    // Shape combo (caller can place this in their layout)
    m_shapeCombo = new QComboBox(this);
    m_shapeCombo->addItem(tr("Sphere"), static_cast<int>(PreviewShape::Sphere));
    m_shapeCombo->addItem(tr("Plane"), static_cast<int>(PreviewShape::Plane));
    m_shapeCombo->addItem(tr("Cube"), static_cast<int>(PreviewShape::Cube));
    m_shapeCombo->addItem(tr("Rounded Cube"), static_cast<int>(PreviewShape::RoundedCube));
    m_shapeCombo->setCurrentIndex(0);

    connect(m_shapeCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                if (m_renderer && index >= 0)
                {
                    auto shape = static_cast<PreviewShape>(m_shapeCombo->itemData(index).toInt());
                    m_renderer->setMesh(shape);
                }
            });

    // Render timer — 30 FPS
    m_renderTimer = new QTimer(this);
    m_renderTimer->setInterval(33);
    connect(m_renderTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_renderer && m_renderer->isInitialized())
                {
                    m_renderer->render();
                }
            });
}

MaterialPreviewWidget::~MaterialPreviewWidget()
{
    m_renderTimer->stop();
}

void MaterialPreviewWidget::initRenderer()
{
    if (m_renderer)
        return;

    m_renderer = std::make_unique<D3D12Renderer>();
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (m_renderer->init(hwnd, static_cast<uint32_t>(width()), static_cast<uint32_t>(height())))
    {
        m_renderer->setCamera(m_azimuth, m_elevation, m_distance);
        m_renderer->setLightDirection(0.5f, 0.7f, 0.5f);
        m_renderTimer->start();
    }
}

void MaterialPreviewWidget::setShape(PreviewShape shape)
{
    if (m_renderer)
        m_renderer->setMesh(shape);

    int index = m_shapeCombo->findData(static_cast<int>(shape));
    if (index >= 0)
        m_shapeCombo->setCurrentIndex(index);
}

void MaterialPreviewWidget::setTextures(const uint8_t* diffuseRGBA, int dw, int dh, const uint8_t* normalRGBA, int nw,
                                        int nh, const uint8_t* rmaosRGBA, int rw, int rh)
{
    if (!m_renderer)
        initRenderer();
    if (m_renderer)
        m_renderer->setTextures(diffuseRGBA, dw, dh, normalRGBA, nw, nh, rmaosRGBA, rw, rh);
}

void MaterialPreviewWidget::setMaterialParams(float specularLevel, float roughnessScale)
{
    if (m_renderer)
        m_renderer->setMaterialParams(specularLevel, roughnessScale);
}

void MaterialPreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_renderer && m_renderer->isInitialized())
    {
        m_renderer->resize(static_cast<uint32_t>(event->size().width()), static_cast<uint32_t>(event->size().height()));
    }
}

void MaterialPreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    if (!m_renderer)
        initRenderer();

    if (m_renderer && m_renderer->isInitialized())
        m_renderer->render();
}

void MaterialPreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_lastMousePos = event->pos();
    }
}

void MaterialPreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;

    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    m_azimuth += delta.x() * 0.01f;
    m_elevation += delta.y() * 0.01f;

    // Clamp elevation to avoid flipping
    m_elevation = std::clamp(m_elevation, -1.5f, 1.5f);

    if (m_renderer)
        m_renderer->setCamera(m_azimuth, m_elevation, m_distance);
}

void MaterialPreviewWidget::wheelEvent(QWheelEvent* event)
{
    float delta = event->angleDelta().y() > 0 ? -0.2f : 0.2f;
    m_distance = std::clamp(m_distance + delta, 1.0f, 10.0f);

    if (m_renderer)
        m_renderer->setCamera(m_azimuth, m_elevation, m_distance);
}

} // namespace tpbr
