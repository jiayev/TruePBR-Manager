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
/// Supports orbit camera via mouse drag and shape selection.
class MaterialPreviewWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit MaterialPreviewWidget(QWidget* parent = nullptr);
    ~MaterialPreviewWidget() override;

    /// Set the preview mesh shape.
    void setShape(PreviewShape shape);

    /// Load PBR textures from RGBA pixel data.
    void setTextures(const uint8_t* diffuseRGBA, int dw, int dh, const uint8_t* normalRGBA, int nw, int nh,
                     const uint8_t* rmaosRGBA, int rw, int rh);

    /// Set material parameters.
    void setMaterialParams(float specularLevel, float roughnessScale);

    /// Get the shape selector combo box (for external layout)
    QComboBox* shapeCombo() const
    {
        return m_shapeCombo;
    }

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    QPaintEngine* paintEngine() const override
    {
        return nullptr;
    } // D3D12 renders directly

  private:
    void initRenderer();

    std::unique_ptr<D3D12Renderer> m_renderer;
    QComboBox* m_shapeCombo = nullptr;
    QTimer* m_renderTimer = nullptr;

    // Camera orbit state
    bool m_dragging = false;
    QPoint m_lastMousePos;
    float m_azimuth = 0.8f;
    float m_elevation = 0.4f;
    float m_distance = 3.0f;
};

} // namespace tpbr
