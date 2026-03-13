#pragma once

#include "renderer/D3D12Renderer.h"
#include "renderer/MeshGenerator.h"

#include <QComboBox>
#include <QTimer>
#include <QWidget>

#include <memory>

namespace tpbr
{

/// Qt widget that hosts a D3D12 PBR material preview.
/// Left-drag: orbit camera. Right-drag: rotate light. Scroll: zoom.
class MaterialPreviewWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit MaterialPreviewWidget(QWidget* parent = nullptr);
    ~MaterialPreviewWidget() override;

    void setShape(PreviewShape shape);
    void setTextures(const uint8_t* diffuseRGBA, int dw, int dh, const uint8_t* normalRGBA, int nw, int nh,
                     const uint8_t* rmaosRGBA, int rw, int rh);
    void setMaterialParams(float specularLevel, float roughnessScale);
    void setRenderFlags(uint32_t flags);
    void setLightColor(float r, float g, float b);
    void setLightIntensity(float intensity);

    /// Load an IBL environment from an HDRI file.
    bool loadIBL(const std::filesystem::path& hdriPath);

    /// Set IBL intensity.
    void setIBLIntensity(float intensity);

    /// Set IBL specular prefilter parameters (triggers reprocessing).
    void setIBLParams(int prefilteredSize, int prefilterSamples);

    QComboBox* shapeCombo() const
    {
        return m_shapeCombo;
    }

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    QPaintEngine* paintEngine() const override
    {
        return nullptr;
    }

  private:
    void initRenderer();
    void updateLight();

    std::unique_ptr<D3D12Renderer> m_renderer;
    QComboBox* m_shapeCombo = nullptr;
    QTimer* m_renderTimer = nullptr;

    // Camera orbit
    bool m_cameraDragging = false;
    QPoint m_lastMousePos;
    float m_azimuth = 0.8f;
    float m_elevation = 0.4f;
    float m_distance = 3.0f;

    // Light orbit
    bool m_lightDragging = false;
    float m_lightAzimuth = 0.6f;
    float m_lightElevation = 0.8f;
};

} // namespace tpbr
