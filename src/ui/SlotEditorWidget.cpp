#include "SlotEditorWidget.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace tpbr {

SlotEditorWidget::SlotEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void SlotEditorWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Required slots (always visible)
    addSlotRow(PBRTextureSlot::Diffuse,  tr("Diffuse"), true);
    addSlotRow(PBRTextureSlot::Normal,   tr("Normal"),  true);
    addSlotRow(PBRTextureSlot::RMAOS,    tr("RMAOS"),   true);

    // Optional slots (visibility toggled by features)
    addSlotRow(PBRTextureSlot::Emissive,            tr("Emissive"),             false);
    addSlotRow(PBRTextureSlot::Displacement,        tr("Displacement"),         false);
    addSlotRow(PBRTextureSlot::Subsurface,          tr("Subsurface"),           false);
    addSlotRow(PBRTextureSlot::CoatNormalRoughness, tr("Coat Normal+Rough"),    false);
    addSlotRow(PBRTextureSlot::Fuzz,                tr("Fuzz"),                 false);
    addSlotRow(PBRTextureSlot::CoatColor,           tr("Coat Color+Strength"),  false);

    for (auto& [slot, row] : m_slotRows) {
        mainLayout->addWidget(row.container);
    }

    mainLayout->addStretch();
}

void SlotEditorWidget::addSlotRow(PBRTextureSlot slot, const QString& label, bool visible)
{
    SlotRow row;
    row.container    = new QWidget(this);
    auto* layout     = new QHBoxLayout(row.container);
    layout->setContentsMargins(0, 2, 0, 2);

    row.labelWidget  = new QLabel(label, row.container);
    row.labelWidget->setFixedWidth(140);
    row.pathLabel    = new QLabel(tr("(empty)"), row.container);
    row.importButton = new QPushButton(tr("Import..."), row.container);

    layout->addWidget(row.labelWidget);
    layout->addWidget(row.pathLabel, 1);
    layout->addWidget(row.importButton);

    row.container->setVisible(visible);

    connect(row.importButton, &QPushButton::clicked, this, [this, slot]() {
        emit importRequested(slot);
    });

    m_slotRows[slot] = row;
}

void SlotEditorWidget::updateSlots(const PBRFeatureFlags& features)
{
    auto show = [&](PBRTextureSlot slot, bool vis) {
        if (m_slotRows.count(slot))
            m_slotRows[slot].container->setVisible(vis);
    };

    show(PBRTextureSlot::Emissive,            features.emissive);
    show(PBRTextureSlot::Displacement,        features.parallax);
    show(PBRTextureSlot::Subsurface,          features.subsurface || features.subsurfaceFoliage);
    show(PBRTextureSlot::CoatNormalRoughness, features.coatNormal);
    show(PBRTextureSlot::Fuzz,                features.fuzz);
    show(PBRTextureSlot::CoatColor,           features.coatDiffuse);
}

void SlotEditorWidget::setTextureSet(const PBRTextureSet& ts)
{
    for (auto& [slot, row] : m_slotRows) {
        auto it = ts.textures.find(slot);
        if (it != ts.textures.end()) {
            row.pathLabel->setText(QString::fromStdString(it->second.sourcePath.filename().string()));
        } else {
            row.pathLabel->setText(tr("(empty)"));
        }
    }
    updateSlots(ts.features);
}

} // namespace tpbr
