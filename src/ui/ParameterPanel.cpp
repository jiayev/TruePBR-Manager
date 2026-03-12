#include "ParameterPanel.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

namespace tpbr {

ParameterPanel::ParameterPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void ParameterPanel::setupUI()
{
    auto* layout = new QFormLayout(this);

    auto addSpin = [&](QDoubleSpinBox*& spin, const QString& label, double min, double max, double step, double val) {
        spin = new QDoubleSpinBox(this);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(3);
        spin->setValue(val);
        layout->addRow(label, spin);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ParameterPanel::onAnyChanged);
    };

    addSpin(m_specularLevel,     tr("Specular Level"),      0.0, 1.0, 0.01, 0.04);
    addSpin(m_roughnessScale,    tr("Roughness Scale"),     0.0, 5.0, 0.1,  1.0);
    addSpin(m_displacementScale, tr("Displacement Scale"),  0.0, 5.0, 0.05, 1.0);
    addSpin(m_subsurfaceOpacity, tr("Subsurface Opacity"),  0.0, 1.0, 0.1,  1.0);
    addSpin(m_emissiveScale,     tr("Emissive Scale"),      0.0, 10.0, 0.1, 0.0);
}

void ParameterPanel::onAnyChanged()
{
    emit parametersChanged(getParameters());
}

void ParameterPanel::setParameters(const PBRParameters& p, const PBRFeatureFlags& features)
{
    m_specularLevel->setValue(p.specularLevel);
    m_roughnessScale->setValue(p.roughnessScale);
    m_displacementScale->setValue(p.displacementScale);
    m_subsurfaceOpacity->setValue(p.subsurfaceOpacity);
    m_emissiveScale->setValue(p.emissiveScale);

    m_displacementScale->setEnabled(features.parallax);
    m_subsurfaceOpacity->setEnabled(features.subsurface);
    m_emissiveScale->setEnabled(features.emissive);
}

PBRParameters ParameterPanel::getParameters() const
{
    PBRParameters p;
    p.specularLevel     = static_cast<float>(m_specularLevel->value());
    p.roughnessScale    = static_cast<float>(m_roughnessScale->value());
    p.displacementScale = static_cast<float>(m_displacementScale->value());
    p.subsurfaceOpacity = static_cast<float>(m_subsurfaceOpacity->value());
    p.emissiveScale     = static_cast<float>(m_emissiveScale->value());
    return p;
}

} // namespace tpbr
