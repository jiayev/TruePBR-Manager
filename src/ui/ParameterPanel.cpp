#include "ParameterPanel.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace tpbr {

ParameterPanel::ParameterPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void ParameterPanel::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Helper to create a spin box and connect its signal
    auto makeSpin = [this](QDoubleSpinBox*& spin, double min, double max, double step, double val, int decimals = 3) {
        spin = new QDoubleSpinBox(this);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setValue(val);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ParameterPanel::onAnyChanged);
    };

    // Helper to create a color row (3 spinboxes in a horizontal layout)
    auto makeColorRow = [&](QDoubleSpinBox*& r, QDoubleSpinBox*& g, QDoubleSpinBox*& b) -> QWidget* {
        auto* w = new QWidget(this);
        auto* lay = new QHBoxLayout(w);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);

        auto addLabel = [&](const QString& text) {
            auto* l = new QLabel(text, w);
            l->setFixedWidth(12);
            lay->addWidget(l);
        };

        addLabel("R"); makeSpin(r, 0.0, 1.0, 0.05, 1.0); lay->addWidget(r);
        addLabel("G"); makeSpin(g, 0.0, 1.0, 0.05, 1.0); lay->addWidget(g);
        addLabel("B"); makeSpin(b, 0.0, 1.0, 0.05, 1.0); lay->addWidget(b);
        return w;
    };

    // ── Base Parameters (always visible) ───────────────────
    auto* baseGroup = new QGroupBox(tr("Base"), this);
    auto* baseLayout = new QFormLayout(baseGroup);
    makeSpin(m_specularLevel,  0.0, 1.0, 0.01, 0.04);
    makeSpin(m_roughnessScale, 0.0, 5.0, 0.1,  1.0);
    baseLayout->addRow(tr("Specular Level"),  m_specularLevel);
    baseLayout->addRow(tr("Roughness Scale"), m_roughnessScale);
    mainLayout->addWidget(baseGroup);

    // ── Parallax Section ───────────────────────────────────
    m_parallaxSection = new QGroupBox(tr("Parallax"), this);
    auto* parallaxLayout = new QFormLayout(m_parallaxSection);
    makeSpin(m_displacementScale, 0.0, 5.0, 0.05, 1.0);
    parallaxLayout->addRow(tr("Displacement Scale"), m_displacementScale);
    m_parallaxSection->setVisible(false);
    mainLayout->addWidget(m_parallaxSection);

    // ── Emissive Section ───────────────────────────────────
    m_emissiveSection = new QGroupBox(tr("Emissive"), this);
    auto* emissiveLayout = new QFormLayout(m_emissiveSection);
    makeSpin(m_emissiveScale, 0.0, 100.0, 0.1, 0.0);
    emissiveLayout->addRow(tr("Emissive Scale"), m_emissiveScale);
    m_emissiveSection->setVisible(false);
    mainLayout->addWidget(m_emissiveSection);

    // ── Subsurface Section ─────────────────────────────────
    m_subsurfaceSection = new QGroupBox(tr("Subsurface"), this);
    auto* sssLayout = new QFormLayout(m_subsurfaceSection);
    makeSpin(m_subsurfaceOpacity, 0.0, 1.0, 0.1, 1.0);
    sssLayout->addRow(tr("Opacity"), m_subsurfaceOpacity);
    sssLayout->addRow(tr("Color"), makeColorRow(m_subsurfaceColorR, m_subsurfaceColorG, m_subsurfaceColorB));
    m_subsurfaceSection->setVisible(false);
    mainLayout->addWidget(m_subsurfaceSection);

    // ── Coat / Multilayer Section ──────────────────────────
    m_coatSection = new QGroupBox(tr("Multilayer / Coat"), this);
    auto* coatLayout = new QFormLayout(m_coatSection);
    makeSpin(m_coatStrength,      0.0, 1.0, 0.1,  0.0);
    makeSpin(m_coatRoughness,     0.0, 1.0, 0.1,  0.0);
    makeSpin(m_coatSpecularLevel, 0.0, 1.0, 0.01, 0.04);
    coatLayout->addRow(tr("Coat Strength"),       m_coatStrength);
    coatLayout->addRow(tr("Coat Roughness"),      m_coatRoughness);
    coatLayout->addRow(tr("Coat Specular Level"), m_coatSpecularLevel);
    m_coatSection->setVisible(false);
    mainLayout->addWidget(m_coatSection);

    // ── Fuzz Section ───────────────────────────────────────
    m_fuzzSection = new QGroupBox(tr("Fuzz"), this);
    auto* fuzzLayout = new QFormLayout(m_fuzzSection);
    makeSpin(m_fuzzWeight, 0.0, 1.0, 0.1, 1.0);
    fuzzLayout->addRow(tr("Weight"), m_fuzzWeight);
    fuzzLayout->addRow(tr("Color"), makeColorRow(m_fuzzColorR, m_fuzzColorG, m_fuzzColorB));
    m_fuzzSection->setVisible(false);
    mainLayout->addWidget(m_fuzzSection);

    // ── Glint Section ──────────────────────────────────────
    m_glintSection = new QGroupBox(tr("Glint"), this);
    auto* glintLayout = new QFormLayout(m_glintSection);
    makeSpin(m_glintScreenSpaceScale,     0.0, 100.0, 0.5, 1.5,  2);
    makeSpin(m_glintLogMicrofacetDensity, 0.0, 40.0,  1.0, 40.0, 1);
    makeSpin(m_glintMicrofacetRoughness,  0.0, 1.0,   0.05, 0.015, 3);
    makeSpin(m_glintDensityRandomization, 0.0, 1000.0, 10.0, 2.0, 1);
    glintLayout->addRow(tr("Screen Space Scale"),      m_glintScreenSpaceScale);
    glintLayout->addRow(tr("Log Microfacet Density"),  m_glintLogMicrofacetDensity);
    glintLayout->addRow(tr("Microfacet Roughness"),    m_glintMicrofacetRoughness);
    glintLayout->addRow(tr("Density Randomization"),   m_glintDensityRandomization);
    m_glintSection->setVisible(false);
    mainLayout->addWidget(m_glintSection);

    mainLayout->addStretch();
}

void ParameterPanel::onAnyChanged()
{
    emit parametersChanged(getParameters());
}

void ParameterPanel::setParameters(const PBRParameters& p, const PBRFeatureFlags& f)
{
    // Block signals to avoid feedback loop
    auto block = [](QDoubleSpinBox* s, double v) {
        s->blockSignals(true);
        s->setValue(v);
        s->blockSignals(false);
    };

    // Base
    block(m_specularLevel,  p.specularLevel);
    block(m_roughnessScale, p.roughnessScale);

    // Parallax
    block(m_displacementScale, p.displacementScale);
    m_parallaxSection->setVisible(f.parallax || f.coatParallax);

    // Emissive
    block(m_emissiveScale, p.emissiveScale);
    m_emissiveSection->setVisible(f.emissive);

    // Subsurface
    block(m_subsurfaceOpacity, p.subsurfaceOpacity);
    block(m_subsurfaceColorR,  p.subsurfaceColor[0]);
    block(m_subsurfaceColorG,  p.subsurfaceColor[1]);
    block(m_subsurfaceColorB,  p.subsurfaceColor[2]);
    m_subsurfaceSection->setVisible(f.subsurface || f.subsurfaceFoliage);

    // Coat
    block(m_coatStrength,      p.coatStrength);
    block(m_coatRoughness,     p.coatRoughness);
    block(m_coatSpecularLevel, p.coatSpecularLevel);
    m_coatSection->setVisible(f.multilayer);

    // Fuzz
    block(m_fuzzWeight,  p.fuzzWeight);
    block(m_fuzzColorR,  p.fuzzColor[0]);
    block(m_fuzzColorG,  p.fuzzColor[1]);
    block(m_fuzzColorB,  p.fuzzColor[2]);
    m_fuzzSection->setVisible(f.fuzz);

    // Glint
    block(m_glintScreenSpaceScale,     p.glintScreenSpaceScale);
    block(m_glintLogMicrofacetDensity, p.glintLogMicrofacetDensity);
    block(m_glintMicrofacetRoughness,  p.glintMicrofacetRoughness);
    block(m_glintDensityRandomization, p.glintDensityRandomization);
    m_glintSection->setVisible(f.glint);
}

PBRParameters ParameterPanel::getParameters() const
{
    PBRParameters p;

    p.specularLevel     = static_cast<float>(m_specularLevel->value());
    p.roughnessScale    = static_cast<float>(m_roughnessScale->value());
    p.displacementScale = static_cast<float>(m_displacementScale->value());
    p.emissiveScale     = static_cast<float>(m_emissiveScale->value());

    p.subsurfaceOpacity  = static_cast<float>(m_subsurfaceOpacity->value());
    p.subsurfaceColor[0] = static_cast<float>(m_subsurfaceColorR->value());
    p.subsurfaceColor[1] = static_cast<float>(m_subsurfaceColorG->value());
    p.subsurfaceColor[2] = static_cast<float>(m_subsurfaceColorB->value());

    p.coatStrength      = static_cast<float>(m_coatStrength->value());
    p.coatRoughness     = static_cast<float>(m_coatRoughness->value());
    p.coatSpecularLevel = static_cast<float>(m_coatSpecularLevel->value());

    p.fuzzWeight   = static_cast<float>(m_fuzzWeight->value());
    p.fuzzColor[0] = static_cast<float>(m_fuzzColorR->value());
    p.fuzzColor[1] = static_cast<float>(m_fuzzColorG->value());
    p.fuzzColor[2] = static_cast<float>(m_fuzzColorB->value());

    p.glintScreenSpaceScale     = static_cast<float>(m_glintScreenSpaceScale->value());
    p.glintLogMicrofacetDensity = static_cast<float>(m_glintLogMicrofacetDensity->value());
    p.glintMicrofacetRoughness  = static_cast<float>(m_glintMicrofacetRoughness->value());
    p.glintDensityRandomization = static_cast<float>(m_glintDensityRandomization->value());

    return p;
}

} // namespace tpbr
