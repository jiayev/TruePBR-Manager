#pragma once

#include "core/PBRTextureSet.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QWidget>

namespace tpbr {

/// Panel for editing all PBR numeric parameters, with sections shown/hidden
/// based on feature flags.
class ParameterPanel : public QWidget {
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

    // Base
    QDoubleSpinBox* m_specularLevel     = nullptr;
    QDoubleSpinBox* m_roughnessScale    = nullptr;

    // Parallax
    QWidget*        m_parallaxSection   = nullptr;
    QDoubleSpinBox* m_displacementScale = nullptr;

    // Emissive
    QWidget*        m_emissiveSection   = nullptr;
    QDoubleSpinBox* m_emissiveScale     = nullptr;

    // Subsurface
    QWidget*        m_subsurfaceSection    = nullptr;
    QDoubleSpinBox* m_subsurfaceOpacity    = nullptr;
    QDoubleSpinBox* m_subsurfaceColorR     = nullptr;
    QDoubleSpinBox* m_subsurfaceColorG     = nullptr;
    QDoubleSpinBox* m_subsurfaceColorB     = nullptr;

    // Coat / Multilayer
    QWidget*        m_coatSection          = nullptr;
    QDoubleSpinBox* m_coatStrength         = nullptr;
    QDoubleSpinBox* m_coatRoughness        = nullptr;
    QDoubleSpinBox* m_coatSpecularLevel    = nullptr;

    // Fuzz
    QWidget*        m_fuzzSection          = nullptr;
    QDoubleSpinBox* m_fuzzColorR           = nullptr;
    QDoubleSpinBox* m_fuzzColorG           = nullptr;
    QDoubleSpinBox* m_fuzzColorB           = nullptr;
    QDoubleSpinBox* m_fuzzWeight           = nullptr;

    // Glint
    QWidget*        m_glintSection                = nullptr;
    QDoubleSpinBox* m_glintScreenSpaceScale       = nullptr;
    QDoubleSpinBox* m_glintLogMicrofacetDensity   = nullptr;
    QDoubleSpinBox* m_glintMicrofacetRoughness    = nullptr;
    QDoubleSpinBox* m_glintDensityRandomization   = nullptr;
};

} // namespace tpbr
