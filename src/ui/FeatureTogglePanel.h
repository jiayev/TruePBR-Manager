#pragma once

#include "core/PBRTextureSet.h"

#include <QCheckBox>
#include <QWidget>

namespace tpbr {

/// Panel with checkboxes for PBR feature toggles (emissive, parallax, subsurface, etc.)
class FeatureTogglePanel : public QWidget {
    Q_OBJECT

public:
    explicit FeatureTogglePanel(QWidget* parent = nullptr);

    void setFeatures(const PBRFeatureFlags& flags);
    PBRFeatureFlags getFeatures() const;

signals:
    void featuresChanged(const PBRFeatureFlags& flags);

private:
    QCheckBox* m_emissive       = nullptr;
    QCheckBox* m_parallax       = nullptr;
    QCheckBox* m_subsurface     = nullptr;
    QCheckBox* m_subsurfaceFoliage = nullptr;
    QCheckBox* m_multilayer     = nullptr;
    QCheckBox* m_coatDiffuse    = nullptr;
    QCheckBox* m_coatParallax   = nullptr;
    QCheckBox* m_coatNormal     = nullptr;
    QCheckBox* m_fuzz           = nullptr;
    QCheckBox* m_glint          = nullptr;
    QCheckBox* m_hair           = nullptr;

    void setupUI();
    void onAnyToggled();
};

} // namespace tpbr
