#pragma once

#include "core/PBRTextureSet.h"

#include <QDoubleSpinBox>
#include <QWidget>

namespace tpbr {

/// Panel for editing PBR numeric parameters (specular level, roughness scale, etc.)
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

    QDoubleSpinBox* m_specularLevel     = nullptr;
    QDoubleSpinBox* m_roughnessScale    = nullptr;
    QDoubleSpinBox* m_displacementScale = nullptr;
    QDoubleSpinBox* m_subsurfaceOpacity = nullptr;
    QDoubleSpinBox* m_emissiveScale     = nullptr;

    // TODO: Add more parameter widgets for coat, fuzz, glint, etc.
};

} // namespace tpbr
