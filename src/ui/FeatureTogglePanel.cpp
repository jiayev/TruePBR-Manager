#include "FeatureTogglePanel.h"

#include <QEvent>
#include <QGroupBox>
#include <QVBoxLayout>

namespace tpbr
{

FeatureTogglePanel::FeatureTogglePanel(QWidget* parent) : QWidget(parent)
{
    setupUI();
}

void FeatureTogglePanel::setupUI()
{
    auto* layout = new QVBoxLayout(this);

    auto addCheck = [&](QCheckBox*& cb, const QString& text)
    {
        cb = new QCheckBox(text, this);
        layout->addWidget(cb);
        connect(cb, &QCheckBox::toggled, this, &FeatureTogglePanel::onAnyToggled);
    };

    addCheck(m_emissive, tr("Emissive / Glow"));
    addCheck(m_parallax, tr("Parallax (Displacement)"));
    addCheck(m_subsurface, tr("Subsurface Scattering"));
    addCheck(m_subsurfaceFoliage, tr("Two-Sided Foliage"));
    addCheck(m_multilayer, tr("Multilayer Parallax"));
    addCheck(m_coatNormal, tr("Coat Normal"));
    addCheck(m_coatDiffuse, tr("Coat Diffuse Color"));
    addCheck(m_coatParallax, tr("Coat Parallax"));
    addCheck(m_fuzz, tr("Fuzz (Cloth/Velvet)"));
    addCheck(m_glint, tr("Glint (Sparkle)"));
    addCheck(m_hair, tr("Hair Model"));

    layout->addStretch();
}

void FeatureTogglePanel::onAnyToggled()
{
    emit featuresChanged(getFeatures());
}

void FeatureTogglePanel::setFeatures(const PBRFeatureFlags& f)
{
    // Block signals to avoid writing partially-updated flags back to the model
    // when switching between texture sets.
    QCheckBox* all[] = {m_emissive,   m_parallax,   m_subsurface,  m_subsurfaceFoliage,
                        m_multilayer, m_coatNormal, m_coatDiffuse, m_coatParallax,
                        m_fuzz,       m_glint,      m_hair};
    for (auto* cb : all)
        cb->blockSignals(true);

    m_emissive->setChecked(f.emissive);
    m_parallax->setChecked(f.parallax);
    m_subsurface->setChecked(f.subsurface);
    m_subsurfaceFoliage->setChecked(f.subsurfaceFoliage);
    m_multilayer->setChecked(f.multilayer);
    m_coatNormal->setChecked(f.coatNormal);
    m_coatDiffuse->setChecked(f.coatDiffuse);
    m_coatParallax->setChecked(f.coatParallax);
    m_fuzz->setChecked(f.fuzz);
    m_glint->setChecked(f.glint);
    m_hair->setChecked(f.hair);

    for (auto* cb : all)
        cb->blockSignals(false);
}

PBRFeatureFlags FeatureTogglePanel::getFeatures() const
{
    PBRFeatureFlags f;
    f.emissive = m_emissive->isChecked();
    f.parallax = m_parallax->isChecked();
    f.subsurface = m_subsurface->isChecked();
    f.subsurfaceFoliage = m_subsurfaceFoliage->isChecked();
    f.multilayer = m_multilayer->isChecked();
    f.coatNormal = m_coatNormal->isChecked();
    f.coatDiffuse = m_coatDiffuse->isChecked();
    f.coatParallax = m_coatParallax->isChecked();
    f.fuzz = m_fuzz->isChecked();
    f.glint = m_glint->isChecked();
    f.hair = m_hair->isChecked();
    return f;
}

void FeatureTogglePanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void FeatureTogglePanel::retranslateUi()
{
    m_emissive->setText(tr("Emissive / Glow"));
    m_parallax->setText(tr("Parallax (Displacement)"));
    m_subsurface->setText(tr("Subsurface Scattering"));
    m_subsurfaceFoliage->setText(tr("Two-Sided Foliage"));
    m_multilayer->setText(tr("Multilayer Parallax"));
    m_coatNormal->setText(tr("Coat Normal"));
    m_coatDiffuse->setText(tr("Coat Diffuse Color"));
    m_coatParallax->setText(tr("Coat Parallax"));
    m_fuzz->setText(tr("Fuzz (Cloth/Velvet)"));
    m_glint->setText(tr("Glint (Sparkle)"));
    m_hair->setText(tr("Hair Model"));
}

} // namespace tpbr
