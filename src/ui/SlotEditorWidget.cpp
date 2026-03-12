#include "SlotEditorWidget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
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

    // ── Match Texture Path ─────────────────────────────────
    auto* matchLayout = new QHBoxLayout();
    auto* matchLabel  = new QLabel(tr("Vanilla Texture:"), this);
    matchLabel->setFixedWidth(140);
    m_matchTextureEdit = new QLineEdit(this);
    m_matchTextureEdit->setPlaceholderText(tr("e.g. architecture\\whiterun\\wrwoodplank01"));
    matchLayout->addWidget(matchLabel);
    matchLayout->addWidget(m_matchTextureEdit, 1);
    mainLayout->addLayout(matchLayout);

    connect(m_matchTextureEdit, &QLineEdit::editingFinished, this, [this]() {
        emit matchTextureChanged(m_matchTextureEdit->text());
    });

    // ── Separator ──────────────────────────────────────────
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(separator);

    // ── Required Texture Slots ─────────────────────────────
    addSlotRow(PBRTextureSlot::Diffuse,  tr("Diffuse"), true);
    addSlotRow(PBRTextureSlot::Normal,   tr("Normal"),  true);
    addSlotRow(PBRTextureSlot::RMAOS,    tr("RMAOS"),   true);

    // ── RMAOS Channel Import Section ───────────────────────
    m_channelSection = new QGroupBox(tr("RMAOS Individual Channels (optional)"), this);
    auto* channelLayout = new QVBoxLayout(m_channelSection);
    channelLayout->setContentsMargins(8, 8, 8, 8);
    channelLayout->setSpacing(2);

    addChannelRow(ChannelMap::Roughness, tr("  Roughness (R)"));
    addChannelRow(ChannelMap::Metallic,  tr("  Metallic (G)"));
    addChannelRow(ChannelMap::AO,        tr("  AO (B)"));
    addChannelRow(ChannelMap::Specular,  tr("  Specular (A)"));

    for (auto& [ch, row] : m_channelRows) {
        channelLayout->addWidget(row.container);
    }

    // ── Optional Texture Slots ─────────────────────────────
    addSlotRow(PBRTextureSlot::Emissive,            tr("Emissive"),             false);
    addSlotRow(PBRTextureSlot::Displacement,        tr("Displacement"),         false);
    addSlotRow(PBRTextureSlot::Subsurface,          tr("Subsurface"),           false);
    addSlotRow(PBRTextureSlot::CoatNormalRoughness, tr("Coat Normal+Rough"),    false);
    addSlotRow(PBRTextureSlot::Fuzz,                tr("Fuzz"),                 false);
    addSlotRow(PBRTextureSlot::CoatColor,           tr("Coat Color+Strength"),  false);

    // Add all slot rows to layout
    for (auto& [slot, row] : m_slotRows) {
        mainLayout->addWidget(row.container);
        // Insert channel section right after RMAOS row
        if (slot == PBRTextureSlot::RMAOS) {
            mainLayout->addWidget(m_channelSection);
        }
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

void SlotEditorWidget::addChannelRow(ChannelMap channel, const QString& label)
{
    ChannelRow row;
    row.container    = new QWidget(this);
    auto* layout     = new QHBoxLayout(row.container);
    layout->setContentsMargins(0, 1, 0, 1);

    row.labelWidget  = new QLabel(label, row.container);
    row.labelWidget->setFixedWidth(140);
    row.pathLabel    = new QLabel(tr("(empty)"), row.container);
    row.importButton = new QPushButton(tr("Import..."), row.container);

    layout->addWidget(row.labelWidget);
    layout->addWidget(row.pathLabel, 1);
    layout->addWidget(row.importButton);

    connect(row.importButton, &QPushButton::clicked, this, [this, channel]() {
        emit importChannelRequested(channel);
    });

    m_channelRows[channel] = row;
}

void SlotEditorWidget::updateSlots(const PBRFeatureFlags& features)
{
    auto show = [&](PBRTextureSlot slot, bool vis) {
        if (m_slotRows.count(slot))
            m_slotRows[slot].container->setVisible(vis);
    };

    show(PBRTextureSlot::Emissive,            features.emissive);
    // Displacement is used by both parallax and coat parallax
    show(PBRTextureSlot::Displacement,        features.parallax || features.coatParallax);
    show(PBRTextureSlot::Subsurface,          features.subsurface || features.subsurfaceFoliage);
    show(PBRTextureSlot::CoatNormalRoughness, features.coatNormal);
    show(PBRTextureSlot::Fuzz,                features.fuzz);
    show(PBRTextureSlot::CoatColor,           features.coatDiffuse);
}

void SlotEditorWidget::setTextureSet(const PBRTextureSet& ts)
{
    // Update match texture path
    m_matchTextureEdit->setText(QString::fromStdString(ts.matchTexture));

    // Update slot rows
    for (auto& [slot, row] : m_slotRows) {
        auto it = ts.textures.find(slot);
        if (it != ts.textures.end()) {
            row.pathLabel->setText(QString::fromStdString(it->second.sourcePath.filename().string()));
        } else {
            row.pathLabel->setText(tr("(empty)"));
        }
    }

    // Update channel rows
    for (auto& [ch, row] : m_channelRows) {
        auto it = ts.channelMaps.find(ch);
        if (it != ts.channelMaps.end()) {
            row.pathLabel->setText(QString::fromStdString(it->second.filename().string()));
        } else {
            row.pathLabel->setText(tr("(empty)"));
        }
    }

    updateSlots(ts.features);
}

} // namespace tpbr
