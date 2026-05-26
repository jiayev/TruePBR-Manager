#include "ParameterPanel.h"

#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>

namespace tpbr
{

ParameterPanel::ParameterPanel(QWidget* parent) : QWidget(parent)
{
    setupUI();
}

QWidget* ParameterPanel::makeColorRow(ColorRow& row)
{
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    auto addLabel = [&](const QString& text)
    {
        auto* l = new QLabel(text, w);
        l->setFixedWidth(12);
        lay->addWidget(l);
    };

    auto makeSpin = [&](QDoubleSpinBox*& spin)
    {
        spin = new QDoubleSpinBox(this);
        spin->setRange(0.0, 1.0);
        spin->setSingleStep(0.05);
        spin->setDecimals(3);
        spin->setValue(1.0);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ParameterPanel::onAnyChanged);
        lay->addWidget(spin);
    };

    addLabel("R");
    makeSpin(row.r);
    addLabel("G");
    makeSpin(row.g);
    addLabel("B");
    makeSpin(row.b);

    // Mode toggle button
    row.modeButton = new QPushButton("0-1", w);
    row.modeButton->setFixedWidth(38);
    row.modeButton->setToolTip(tr("Toggle between 0.0-1.0 and 0-255 display"));
    row.intMode = false;
    lay->addWidget(row.modeButton);
    connect(row.modeButton, &QPushButton::clicked, this, [this, &row]() { toggleColorMode(row); });

    return w;
}

void ParameterPanel::toggleColorMode(ColorRow& row)
{
    // Read current float values before switching
    float rv, gv, bv;
    if (row.intMode)
    {
        // Currently in int mode — convert back to float for reading
        rv = static_cast<float>(row.r->value() / 255.0);
        gv = static_cast<float>(row.g->value() / 255.0);
        bv = static_cast<float>(row.b->value() / 255.0);
    }
    else
    {
        rv = static_cast<float>(row.r->value());
        gv = static_cast<float>(row.g->value());
        bv = static_cast<float>(row.b->value());
    }

    row.intMode = !row.intMode;

    // Block signals during reconfiguration
    row.r->blockSignals(true);
    row.g->blockSignals(true);
    row.b->blockSignals(true);

    if (row.intMode)
    {
        row.modeButton->setText("0-255");
        for (auto* spin : {row.r, row.g, row.b})
        {
            spin->setRange(0.0, 255.0);
            spin->setSingleStep(1.0);
            spin->setDecimals(0);
        }
        row.r->setValue(std::round(rv * 255.0));
        row.g->setValue(std::round(gv * 255.0));
        row.b->setValue(std::round(bv * 255.0));
    }
    else
    {
        row.modeButton->setText("0-1");
        for (auto* spin : {row.r, row.g, row.b})
        {
            spin->setRange(0.0, 1.0);
            spin->setSingleStep(0.05);
            spin->setDecimals(3);
        }
        row.r->setValue(rv);
        row.g->setValue(gv);
        row.b->setValue(bv);
    }

    row.r->blockSignals(false);
    row.g->blockSignals(false);
    row.b->blockSignals(false);
}

void ParameterPanel::setColorRowValues(ColorRow& row, float rv, float gv, float bv)
{
    row.r->blockSignals(true);
    row.g->blockSignals(true);
    row.b->blockSignals(true);

    if (row.intMode)
    {
        row.r->setValue(std::round(rv * 255.0));
        row.g->setValue(std::round(gv * 255.0));
        row.b->setValue(std::round(bv * 255.0));
    }
    else
    {
        row.r->setValue(rv);
        row.g->setValue(gv);
        row.b->setValue(bv);
    }

    row.r->blockSignals(false);
    row.g->blockSignals(false);
    row.b->blockSignals(false);
}

void ParameterPanel::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Helper to create a spin box and connect its signal
    auto makeSpin = [this](QDoubleSpinBox*& spin, double min, double max, double step, double val, int decimals = 3)
    {
        spin = new QDoubleSpinBox(this);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setValue(val);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ParameterPanel::onAnyChanged);
    };

    // ── Base Parameters (always visible) ───────────────────
    m_baseGroup = new QGroupBox(tr("Base"), this);
    auto* baseLayout = new QFormLayout(m_baseGroup);
    makeSpin(m_specularLevel, 0.0, 1.0, 0.01, 0.04);
    makeSpin(m_roughnessScale, 0.0, 5.0, 0.1, 1.0);
    baseLayout->addRow(tr("Specular Level"), m_specularLevel);
    baseLayout->addRow(tr("Roughness Scale"), m_roughnessScale);
    mainLayout->addWidget(m_baseGroup);

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
    emissiveLayout->addRow(tr("Color"), makeColorRow(m_emissiveColorRow));
    m_emissiveSection->setVisible(false);
    mainLayout->addWidget(m_emissiveSection);

    // ── Subsurface Section ─────────────────────────────────
    m_subsurfaceSection = new QGroupBox(tr("Subsurface"), this);
    auto* sssLayout = new QFormLayout(m_subsurfaceSection);
    makeSpin(m_subsurfaceOpacity, 0.0, 1.0, 0.1, 1.0);
    sssLayout->addRow(tr("Opacity"), m_subsurfaceOpacity);
    sssLayout->addRow(tr("Color"), makeColorRow(m_subsurfaceColorRow));
    m_subsurfaceSection->setVisible(false);
    mainLayout->addWidget(m_subsurfaceSection);

    // ── Coat / Multilayer Section ──────────────────────────
    m_coatSection = new QGroupBox(tr("Multilayer / Coat"), this);
    auto* coatLayout = new QFormLayout(m_coatSection);
    makeSpin(m_coatStrength, 0.0, 1.0, 0.1, 0.0);
    makeSpin(m_coatRoughness, 0.0, 1.0, 0.1, 0.0);
    makeSpin(m_coatSpecularLevel, 0.0, 1.0, 0.01, 0.04);
    coatLayout->addRow(tr("Coat Strength"), m_coatStrength);
    coatLayout->addRow(tr("Coat Roughness"), m_coatRoughness);
    coatLayout->addRow(tr("Coat Specular Level"), m_coatSpecularLevel);
    m_coatSection->setVisible(false);
    mainLayout->addWidget(m_coatSection);

    // ── Fuzz Section ───────────────────────────────────────
    m_fuzzSection = new QGroupBox(tr("Fuzz"), this);
    auto* fuzzLayout = new QFormLayout(m_fuzzSection);
    makeSpin(m_fuzzWeight, 0.0, 1.0, 0.1, 1.0);
    fuzzLayout->addRow(tr("Weight"), m_fuzzWeight);
    fuzzLayout->addRow(tr("Color"), makeColorRow(m_fuzzColorRow));
    m_fuzzSection->setVisible(false);
    mainLayout->addWidget(m_fuzzSection);

    // ── Glint Section ──────────────────────────────────────
    m_glintSection = new QGroupBox(tr("Glint"), this);
    auto* glintLayout = new QFormLayout(m_glintSection);
    makeSpin(m_glintScreenSpaceScale, 0.0, 100.0, 0.5, 1.5, 2);
    makeSpin(m_glintLogMicrofacetDensity, 0.0, 40.0, 1.0, 40.0, 1);
    makeSpin(m_glintMicrofacetRoughness, 0.0, 1.0, 0.05, 0.015, 3);
    makeSpin(m_glintDensityRandomization, 0.0, 1000.0, 10.0, 2.0, 1);
    glintLayout->addRow(tr("Screen Space Scale"), m_glintScreenSpaceScale);
    glintLayout->addRow(tr("Log Microfacet Density"), m_glintLogMicrofacetDensity);
    glintLayout->addRow(tr("Microfacet Roughness"), m_glintMicrofacetRoughness);
    glintLayout->addRow(tr("Density Randomization"), m_glintDensityRandomization);
    m_glintSection->setVisible(false);
    mainLayout->addWidget(m_glintSection);

    // ── Vertex Color Section (always visible) ──────────────
    m_vertexColorSection = new QGroupBox(tr("Vertex Color"), this);
    auto* vcLayout = new QFormLayout(m_vertexColorSection);
    m_vertexColorsEnabled = new QCheckBox(tr("Enable Vertex Colors"), this);
    m_vertexColorsEnabled->setChecked(true);
    connect(m_vertexColorsEnabled, &QCheckBox::toggled, this, &ParameterPanel::onAnyChanged);
    vcLayout->addRow(m_vertexColorsEnabled);
    makeSpin(m_vertexColorLumMult, 0.0, 5.0, 0.1, 1.0, 2);
    makeSpin(m_vertexColorSatMult, 0.0, 5.0, 0.1, 1.0, 2);
    vcLayout->addRow(tr("Luminance Multiplier"), m_vertexColorLumMult);
    vcLayout->addRow(tr("Saturation Multiplier"), m_vertexColorSatMult);
    mainLayout->addWidget(m_vertexColorSection);

    mainLayout->addStretch();
}

void ParameterPanel::onAnyChanged()
{
    emit parametersChanged(getParameters());
}

void ParameterPanel::setParameters(const PBRParameters& p, const PBRFeatureFlags& f)
{
    // Block signals to avoid feedback loop
    auto block = [](QDoubleSpinBox* s, double v)
    {
        s->blockSignals(true);
        s->setValue(v);
        s->blockSignals(false);
    };

    // Base
    block(m_specularLevel, p.specularLevel);
    block(m_roughnessScale, p.roughnessScale);

    // Parallax
    block(m_displacementScale, p.displacementScale);
    m_parallaxSection->setVisible(f.parallax || f.coatParallax);

    // Emissive
    block(m_emissiveScale, p.emissiveScale);
    setColorRowValues(m_emissiveColorRow, p.emissiveColor[0], p.emissiveColor[1], p.emissiveColor[2]);
    m_emissiveSection->setVisible(f.emissive);

    // Subsurface
    block(m_subsurfaceOpacity, p.subsurfaceOpacity);
    setColorRowValues(m_subsurfaceColorRow, p.subsurfaceColor[0], p.subsurfaceColor[1], p.subsurfaceColor[2]);
    m_subsurfaceSection->setVisible(f.subsurface || f.subsurfaceFoliage);

    // Coat
    block(m_coatStrength, p.coatStrength);
    block(m_coatRoughness, p.coatRoughness);
    block(m_coatSpecularLevel, p.coatSpecularLevel);
    m_coatSection->setVisible(f.multilayer);

    // Fuzz
    block(m_fuzzWeight, p.fuzzWeight);
    setColorRowValues(m_fuzzColorRow, p.fuzzColor[0], p.fuzzColor[1], p.fuzzColor[2]);
    m_fuzzSection->setVisible(f.fuzz);

    // Glint
    block(m_glintScreenSpaceScale, p.glintScreenSpaceScale);
    block(m_glintLogMicrofacetDensity, p.glintLogMicrofacetDensity);
    block(m_glintMicrofacetRoughness, p.glintMicrofacetRoughness);
    block(m_glintDensityRandomization, p.glintDensityRandomization);
    m_glintSection->setVisible(f.glint);

    // Vertex Color
    m_vertexColorsEnabled->blockSignals(true);
    m_vertexColorsEnabled->setChecked(p.vertexColors);
    m_vertexColorsEnabled->blockSignals(false);
    block(m_vertexColorLumMult, p.vertexColorLumMult);
    block(m_vertexColorSatMult, p.vertexColorSatMult);
}

PBRParameters ParameterPanel::getParameters() const
{
    PBRParameters p;

    p.specularLevel = static_cast<float>(m_specularLevel->value());
    p.roughnessScale = static_cast<float>(m_roughnessScale->value());
    p.displacementScale = static_cast<float>(m_displacementScale->value());
    p.emissiveScale = static_cast<float>(m_emissiveScale->value());

    // Color rows: read float values (convert from int mode if active)
    auto readColor = [](const ColorRow& row, std::array<float, 3>& out)
    {
        if (row.intMode)
        {
            out[0] = static_cast<float>(row.r->value() / 255.0);
            out[1] = static_cast<float>(row.g->value() / 255.0);
            out[2] = static_cast<float>(row.b->value() / 255.0);
        }
        else
        {
            out[0] = static_cast<float>(row.r->value());
            out[1] = static_cast<float>(row.g->value());
            out[2] = static_cast<float>(row.b->value());
        }
    };

    readColor(m_emissiveColorRow, p.emissiveColor);

    p.subsurfaceOpacity = static_cast<float>(m_subsurfaceOpacity->value());
    readColor(m_subsurfaceColorRow, p.subsurfaceColor);

    p.coatStrength = static_cast<float>(m_coatStrength->value());
    p.coatRoughness = static_cast<float>(m_coatRoughness->value());
    p.coatSpecularLevel = static_cast<float>(m_coatSpecularLevel->value());

    p.fuzzWeight = static_cast<float>(m_fuzzWeight->value());
    readColor(m_fuzzColorRow, p.fuzzColor);

    p.glintScreenSpaceScale = static_cast<float>(m_glintScreenSpaceScale->value());
    p.glintLogMicrofacetDensity = static_cast<float>(m_glintLogMicrofacetDensity->value());
    p.glintMicrofacetRoughness = static_cast<float>(m_glintMicrofacetRoughness->value());
    p.glintDensityRandomization = static_cast<float>(m_glintDensityRandomization->value());

    p.vertexColors = m_vertexColorsEnabled->isChecked();
    p.vertexColorLumMult = static_cast<float>(m_vertexColorLumMult->value());
    p.vertexColorSatMult = static_cast<float>(m_vertexColorSatMult->value());

    return p;
}

void ParameterPanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void ParameterPanel::retranslateUi()
{
    // Helper to relabel a QFormLayout row by its field widget
    auto relabelRow = [](QWidget* field, const QString& text)
    {
        if (auto* form = qobject_cast<QFormLayout*>(field->parentWidget()->layout()))
        {
            if (auto* label = qobject_cast<QLabel*>(form->labelForField(field)))
                label->setText(text);
        }
    };

    m_baseGroup->setTitle(tr("Base"));
    relabelRow(m_specularLevel, tr("Specular Level"));
    relabelRow(m_roughnessScale, tr("Roughness Scale"));

    m_parallaxSection->setTitle(tr("Parallax"));
    relabelRow(m_displacementScale, tr("Displacement Scale"));

    m_emissiveSection->setTitle(tr("Emissive"));
    relabelRow(m_emissiveScale, tr("Emissive Scale"));

    m_subsurfaceSection->setTitle(tr("Subsurface"));
    relabelRow(m_subsurfaceOpacity, tr("Opacity"));

    m_coatSection->setTitle(tr("Multilayer / Coat"));
    relabelRow(m_coatStrength, tr("Coat Strength"));
    relabelRow(m_coatRoughness, tr("Coat Roughness"));
    relabelRow(m_coatSpecularLevel, tr("Coat Specular Level"));

    m_fuzzSection->setTitle(tr("Fuzz"));
    relabelRow(m_fuzzWeight, tr("Weight"));

    m_glintSection->setTitle(tr("Glint"));
    relabelRow(m_glintScreenSpaceScale, tr("Screen Space Scale"));
    relabelRow(m_glintLogMicrofacetDensity, tr("Log Microfacet Density"));
    relabelRow(m_glintMicrofacetRoughness, tr("Microfacet Roughness"));
    relabelRow(m_glintDensityRandomization, tr("Density Randomization"));

    m_vertexColorSection->setTitle(tr("Vertex Color"));
    m_vertexColorsEnabled->setText(tr("Enable Vertex Colors"));
    relabelRow(m_vertexColorLumMult, tr("Luminance Multiplier"));
    relabelRow(m_vertexColorSatMult, tr("Saturation Multiplier"));

    // Update color mode button tooltips
    auto updateModeTooltip = [this](ColorRow& row)
    { row.modeButton->setToolTip(tr("Toggle between 0.0-1.0 and 0-255 display")); };
    updateModeTooltip(m_emissiveColorRow);
    updateModeTooltip(m_subsurfaceColorRow);
    updateModeTooltip(m_fuzzColorRow);
}

} // namespace tpbr
