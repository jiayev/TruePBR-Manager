#pragma once

#include "core/PBRTextureSet.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPushButton>
#include <QWidget>

namespace tpbr
{

/// Panel for editing all PBR numeric parameters, with sections shown/hidden
/// based on feature flags.
class ParameterPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit ParameterPanel(QWidget* parent = nullptr);

    void setParameters(const PBRParameters& params, const PBRFeatureFlags& features);
    PBRParameters getParameters() const;

  signals:
    void parametersChanged(const PBRParameters& params);

  private:
    void setupUI();
    void onAnyChanged();
    void retranslateUi();

    /// A color row containing 3 spinboxes and a mode toggle button.
    struct ColorRow
    {
        QDoubleSpinBox* r = nullptr;
        QDoubleSpinBox* g = nullptr;
        QDoubleSpinBox* b = nullptr;
        QPushButton* modeButton = nullptr;
        bool intMode = false; // false = 0.0-1.0, true = 0-255
    };

    QWidget* makeColorRow(ColorRow& row);
    void toggleColorMode(ColorRow& row);
    void setColorRowValues(ColorRow& row, float rv, float gv, float bv);

  protected:
    void changeEvent(QEvent* event) override;

  private:
    // Base
    QGroupBox* m_baseGroup = nullptr;
    QDoubleSpinBox* m_specularLevel = nullptr;
    QDoubleSpinBox* m_roughnessScale = nullptr;

    // Parallax
    QGroupBox* m_parallaxSection = nullptr;
    QDoubleSpinBox* m_displacementScale = nullptr;

    // Emissive
    QGroupBox* m_emissiveSection = nullptr;
    QDoubleSpinBox* m_emissiveScale = nullptr;
    ColorRow m_emissiveColorRow;

    // Subsurface
    QGroupBox* m_subsurfaceSection = nullptr;
    QDoubleSpinBox* m_subsurfaceOpacity = nullptr;
    ColorRow m_subsurfaceColorRow;

    // Coat / Multilayer
    QGroupBox* m_coatSection = nullptr;
    QDoubleSpinBox* m_coatStrength = nullptr;
    QDoubleSpinBox* m_coatRoughness = nullptr;
    QDoubleSpinBox* m_coatSpecularLevel = nullptr;

    // Fuzz
    QGroupBox* m_fuzzSection = nullptr;
    ColorRow m_fuzzColorRow;
    QDoubleSpinBox* m_fuzzWeight = nullptr;

    // Glint
    QGroupBox* m_glintSection = nullptr;
    QDoubleSpinBox* m_glintScreenSpaceScale = nullptr;
    QDoubleSpinBox* m_glintLogMicrofacetDensity = nullptr;
    QDoubleSpinBox* m_glintMicrofacetRoughness = nullptr;
    QDoubleSpinBox* m_glintDensityRandomization = nullptr;

    // Vertex Color
    QGroupBox* m_vertexColorSection = nullptr;
    QCheckBox* m_vertexColorsEnabled = nullptr;
    QDoubleSpinBox* m_vertexColorLumMult = nullptr;
    QDoubleSpinBox* m_vertexColorSatMult = nullptr;
};

} // namespace tpbr
