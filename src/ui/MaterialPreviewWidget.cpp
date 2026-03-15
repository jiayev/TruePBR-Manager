#include "MaterialPreviewWidget.h"
#include "utils/Log.h"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace tpbr
{

MaterialPreviewWidget::MaterialPreviewWidget(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMinimumSize(200, 200);

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

    m_renderTimer = new QTimer(this);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
    m_renderTimer->setInterval(1);
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
    spdlog::info("MaterialPreviewWidget: initializing D3D12 renderer ({}x{})", width(), height());
    spdlog::default_logger()->flush();

    if (m_renderer->init(hwnd, static_cast<uint32_t>(width()), static_cast<uint32_t>(height())))
    {
        m_renderer->setCamera(m_azimuth, m_elevation, m_distance);
        updateLight();
        m_renderTimer->start();
        spdlog::info("MaterialPreviewWidget: renderer started");
    }
    else
    {
        spdlog::error("MaterialPreviewWidget: renderer init FAILED");
        m_renderer.reset();
    }
}

void MaterialPreviewWidget::updateLight()
{
    if (!m_renderer)
        return;

    float lx = std::cos(m_lightElevation) * std::sin(m_lightAzimuth);
    float ly = std::sin(m_lightElevation);
    float lz = std::cos(m_lightElevation) * std::cos(m_lightAzimuth);
    m_renderer->setLightDirection(lx, ly, lz);
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

void MaterialPreviewWidget::setFeatureParams(const PBRFeatureFlags& features, const PBRParameters& params)
{
    if (m_renderer)
        m_renderer->setFeatureParams(features, params);
}

void MaterialPreviewWidget::setFeatureTextures(const uint8_t* emissiveRGBA, int ew, int eh, const uint8_t* feat0RGBA,
                                               int f0w, int f0h, const uint8_t* feat1RGBA, int f1w, int f1h)
{
    if (m_renderer)
        m_renderer->setFeatureTextures(emissiveRGBA, ew, eh, feat0RGBA, f0w, f0h, feat1RGBA, f1w, f1h);
}

void MaterialPreviewWidget::setRenderFlags(uint32_t flags)
{
    if (m_renderer)
        m_renderer->setRenderFlags(flags);
}

void MaterialPreviewWidget::setLightColor(float r, float g, float b)
{
    if (m_renderer)
        m_renderer->setLightColor(r, g, b);
}

void MaterialPreviewWidget::setLightIntensity(float intensity)
{
    if (m_renderer)
        m_renderer->setLightIntensity(intensity);
}

bool MaterialPreviewWidget::loadIBL(const std::filesystem::path& hdriPath)
{
    if (!m_renderer)
        initRenderer();
    if (m_renderer)
        return m_renderer->loadIBL(hdriPath);
    return false;
}

void MaterialPreviewWidget::unloadIBL()
{
    if (m_renderer)
        m_renderer->unloadIBL();
}

void MaterialPreviewWidget::setIBLIntensity(float intensity)
{
    if (m_renderer)
        m_renderer->setIBLIntensity(intensity);
}

void MaterialPreviewWidget::setIBLParams(int prefilteredSize, int prefilterSamples)
{
    if (!m_renderer)
        initRenderer();
    if (m_renderer)
        m_renderer->setIBLParams(prefilteredSize, prefilterSamples);
}

HDRDisplayInfo MaterialPreviewWidget::queryHDRSupport() const
{
    if (m_renderer)
        return m_renderer->queryHDRSupport();
    return {};
}

void MaterialPreviewWidget::setVSync(bool enabled)
{
    if (m_renderer)
        m_renderer->setVSync(enabled);
}

void MaterialPreviewWidget::setTAAEnabled(bool enabled)
{
    if (m_renderer)
        m_renderer->setTAAEnabled(enabled);
}

bool MaterialPreviewWidget::isTAAEnabled() const
{
    return m_renderer ? m_renderer->isTAAEnabled() : true;
}

void MaterialPreviewWidget::setHDREnabled(bool enabled)
{
    if (!m_renderer)
        initRenderer();
    if (m_renderer)
        m_renderer->setHDREnabled(enabled);
}

void MaterialPreviewWidget::setPaperWhiteNits(float nits)
{
    if (m_renderer)
        m_renderer->setPaperWhiteNits(nits);
}

void MaterialPreviewWidget::setPeakBrightnessNits(float nits)
{
    if (m_renderer)
        m_renderer->setPeakBrightnessNits(nits);
}

void MaterialPreviewWidget::setExposure(float ev)
{
    if (m_renderer)
        m_renderer->setExposure(ev);
}

bool MaterialPreviewWidget::isHDREnabled() const
{
    return m_renderer ? m_renderer->isHDREnabled() : false;
}

bool MaterialPreviewWidget::isVSyncEnabled() const
{
    return m_renderer ? m_renderer->isVSyncEnabled() : true;
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
    m_lastMousePos = event->pos();

    if (event->button() == Qt::LeftButton)
    {
        m_cameraDragging = true;
    }
    else if (event->button() == Qt::RightButton)
    {
        m_lightDragging = true;
    }
    else if (event->button() == Qt::MiddleButton)
    {
        m_envDragging = true;
    }
}

void MaterialPreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_cameraDragging = false;
    }
    else if (event->button() == Qt::RightButton)
    {
        m_lightDragging = false;
    }
    else if (event->button() == Qt::MiddleButton)
    {
        m_envDragging = false;
    }
}

void MaterialPreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    if (m_cameraDragging)
    {
        m_azimuth += delta.x() * 0.01f;
        m_elevation += delta.y() * 0.01f;
        m_elevation = std::clamp(m_elevation, -1.5f, 1.5f);

        if (m_renderer)
            m_renderer->setCamera(m_azimuth, m_elevation, m_distance);
    }

    if (m_lightDragging)
    {
        m_lightAzimuth += delta.x() * 0.01f;
        m_lightElevation += delta.y() * 0.01f;
        m_lightElevation = std::clamp(m_lightElevation, -1.5f, 1.5f);
        updateLight();
    }

    if (m_envDragging)
    {
        m_envRotation += delta.x() * 0.01f;
        if (m_renderer)
            m_renderer->setEnvRotation(m_envRotation);
    }
}

void MaterialPreviewWidget::wheelEvent(QWheelEvent* event)
{
    float delta = event->angleDelta().y() > 0 ? -0.2f : 0.2f;
    m_distance = std::clamp(m_distance + delta, 1.0f, 10.0f);

    if (m_renderer)
        m_renderer->setCamera(m_azimuth, m_elevation, m_distance);
}

} // namespace tpbr
