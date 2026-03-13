#include "SlotEditorWidget.h"

#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QFontMetrics>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QRadioButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace tpbr
{

static std::vector<DDSCompressionMode> allowedCompressionModesForSlot(PBRTextureSlot slot)
{
    switch (slot)
    {
    case PBRTextureSlot::Diffuse:
    case PBRTextureSlot::CoatColor:
        return {
            DDSCompressionMode::BC7_sRGB,
        };

    case PBRTextureSlot::Subsurface:
    case PBRTextureSlot::Fuzz:
        return {
            DDSCompressionMode::BC7_sRGB,
            DDSCompressionMode::BC3_sRGB,
        };

    case PBRTextureSlot::Emissive:
        return {
            DDSCompressionMode::BC6H_UF16,
            DDSCompressionMode::BC7_sRGB,
        };

    case PBRTextureSlot::Normal:
        return {
            DDSCompressionMode::BC7_Linear,
            DDSCompressionMode::BC5_Linear,
        };

    case PBRTextureSlot::Displacement:
        return {
            DDSCompressionMode::BC4_Linear,
        };

    case PBRTextureSlot::RMAOS:
    case PBRTextureSlot::CoatNormalRoughness:
        return {
            DDSCompressionMode::BC7_Linear,
        };
    }

    return {defaultCompressionForSlot(slot)};
}

static bool isBC1EligibleColorSlot(PBRTextureSlot slot)
{
    switch (slot)
    {
    case PBRTextureSlot::Diffuse:
    case PBRTextureSlot::Subsurface:
    case PBRTextureSlot::Fuzz:
    case PBRTextureSlot::CoatColor:
        return true;
    default:
        return false;
    }
}

static bool canUseBC1(PBRTextureSlot slot, const TextureEntry* entry, const PBRTextureSet* textureSet)
{
    if (slot == PBRTextureSlot::RMAOS && textureSet != nullptr)
    {
        if (textureSet->rmaosSourceMode == RMAOSSourceMode::SeparateChannels)
        {
            const auto specularIt = textureSet->channelMaps.find(ChannelMap::Specular);
            return specularIt == textureSet->channelMaps.end() || specularIt->second.sourcePath.empty();
        }
    }

    if (entry == nullptr)
    {
        return false;
    }

    return entry->alphaMode == TextureAlphaMode::None || entry->alphaMode == TextureAlphaMode::Opaque;
}

static std::vector<DDSCompressionMode> allowedCompressionModesForTexture(PBRTextureSlot slot, const TextureEntry* entry,
                                                                         const PBRTextureSet* textureSet)
{
    auto modes = allowedCompressionModesForSlot(slot);

    if (isBC1EligibleColorSlot(slot) && canUseBC1(slot, entry, textureSet))
    {
        modes.insert(modes.begin() + 1, DDSCompressionMode::BC1_sRGB);
    }
    else if (slot == PBRTextureSlot::RMAOS && canUseBC1(slot, entry, textureSet))
    {
        modes.insert(modes.begin() + 1, DDSCompressionMode::BC1_Linear);
    }

    return modes;
}

static void populateCompressionCombo(QComboBox* combo, PBRTextureSlot slot)
{
    combo->clear();

    const auto modes = allowedCompressionModesForTexture(slot, nullptr, nullptr);

    for (DDSCompressionMode mode : modes)
    {
        combo->addItem(QString::fromUtf8(compressionModeDisplayName(mode)), static_cast<int>(mode));
    }

    const int defaultIndex = combo->findData(static_cast<int>(defaultCompressionForSlot(slot)));
    combo->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
}

static QPixmap loadThumbnailPixmap(const std::filesystem::path& path)
{
    const auto ext = FileUtils::getExtensionLower(path);

    if (ext == ".dds")
    {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgbaPixels;
        if (!DDSUtils::loadDDS(path, width, height, rgbaPixels) || rgbaPixels.empty())
        {
            return {};
        }

        QImage image(rgbaPixels.data(), width, height, width * 4, QImage::Format_RGBA8888);
        return QPixmap::fromImage(image.copy());
    }

    QPixmap pix;
    pix.load(QString::fromStdString(path.string()));
    return pix;
}

// ─── DropZoneLabel ─────────────────────────────────────────

static bool isSupportedImageFile(const QString& path)
{
    auto lower = path.toLower();
    return lower.endsWith(".png") || lower.endsWith(".dds") || lower.endsWith(".tga") || lower.endsWith(".bmp") ||
           lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

DropZoneLabel::DropZoneLabel(QWidget* parent) : QLabel(parent)
{
    setAcceptDrops(true);
    setMinimumHeight(ThumbnailSize + 8);
    setFixedHeight(ThumbnailSize + 8);
    setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet("DropZoneLabel { background: #2a2a2a; color: #888; padding: 2px 4px; }");
    setToolTip(tr("Click to browse or drop an image here"));
    setText(tr("(drop image here)"));
}

void DropZoneLabel::setFile(const std::filesystem::path& path, const QString& detailText)
{
    m_filename = QString::fromStdString(path.filename().string());
    m_detailText = detailText;

    auto ext = FileUtils::getExtensionLower(path);
    QPixmap pix = loadThumbnailPixmap(path);

    if (!pix.isNull())
    {
        m_thumbnail = pix.scaled(ThumbnailSize, ThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    else
    {
        m_thumbnail = QPixmap(ThumbnailSize, ThumbnailSize);
        m_thumbnail.fill(QColor(60, 60, 80));
        QPainter p(&m_thumbnail);
        p.setPen(Qt::white);
        p.drawText(m_thumbnail.rect(), Qt::AlignCenter, ext == ".dds" ? "DDS" : "?");
    }

    update();
}

void DropZoneLabel::clear()
{
    m_thumbnail = QPixmap();
    m_filename.clear();
    m_detailText.clear();
    setText(tr("(drop image here)"));
    update();
}

void DropZoneLabel::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() && isSupportedImageFile(urls.first().toLocalFile()))
        {
            event->acceptProposedAction();
            m_dragHover = true;
            update();
            return;
        }
    }
    event->ignore();
}

void DropZoneLabel::dragLeaveEvent(QDragLeaveEvent* /*event*/)
{
    m_dragHover = false;
    update();
}

void DropZoneLabel::dropEvent(QDropEvent* event)
{
    m_dragHover = false;
    if (event->mimeData()->hasUrls())
    {
        auto urls = event->mimeData()->urls();
        if (!urls.isEmpty())
        {
            QString filePath = urls.first().toLocalFile();
            if (isSupportedImageFile(filePath))
            {
                emit fileDropped(filePath);
                event->acceptProposedAction();
                update();
                return;
            }
        }
    }
    event->ignore();
}

void DropZoneLabel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
    {
        emit clicked();
        event->accept();
        return;
    }

    QLabel::mouseReleaseEvent(event);
}

void DropZoneLabel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);

    // Background
    QColor bg = m_dragHover ? QColor(50, 60, 80) : QColor(42, 42, 42);
    p.fillRect(rect(), bg);

    // Border
    if (m_dragHover)
    {
        p.setPen(QPen(QColor(80, 140, 220), 2));
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
    else
    {
        p.setPen(QPen(QColor(70, 70, 70), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }

    if (!m_thumbnail.isNull())
    {
        // Draw thumbnail on the left
        int y = (height() - m_thumbnail.height()) / 2;
        p.drawPixmap(4, y, m_thumbnail);

        // Draw filename and detail text in two separate rows.
        const int textLeft = ThumbnailSize + 12;
        const int textWidth = width() - textLeft - 8;
        const QRect titleRect(textLeft, 8, textWidth, 18);
        const QRect detailRect(textLeft, 28, textWidth, 16);
        const QFontMetrics titleMetrics(p.font());
        const QString elidedFilename = titleMetrics.elidedText(m_filename, Qt::ElideRight, titleRect.width());

        p.setPen(QColor(200, 200, 200));
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, elidedFilename);

        if (!m_detailText.isEmpty())
        {
            p.setPen(QColor(150, 150, 150));
            p.drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter, m_detailText);
        }
    }
    else
    {
        // Empty state
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect().adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   m_dragHover ? tr("Drop to import...") : tr("(drop image here)"));
    }
}

// ─── SlotEditorWidget ──────────────────────────────────────

SlotEditorWidget::SlotEditorWidget(QWidget* parent) : QWidget(parent)
{
    setupUI();
}

void SlotEditorWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // ── Match Texture Path ─────────────────────────────────
    auto* matchLayout = new QHBoxLayout();
    auto* matchLabel = new QLabel(tr("Vanilla Match:"), this);
    matchLabel->setFixedWidth(100);
    m_matchTextureEdit = new QLineEdit(this);
    m_matchTextureEdit->setPlaceholderText(tr("e.g. architecture\\whiterun\\wrwoodplank01 or wrwoodplank01_n"));
    m_matchModeCombo = new QComboBox(this);
    m_matchModeCombo->addItem(tr("Auto"), static_cast<int>(TextureMatchMode::Auto));
    m_matchModeCombo->addItem(tr("Match Diffuse"), static_cast<int>(TextureMatchMode::Diffuse));
    m_matchModeCombo->addItem(tr("Match Normal"), static_cast<int>(TextureMatchMode::Normal));
    matchLayout->addWidget(matchLabel);
    matchLayout->addWidget(m_matchTextureEdit, 1);
    matchLayout->addWidget(m_matchModeCombo);
    mainLayout->addLayout(matchLayout);

    connect(m_matchTextureEdit, &QLineEdit::editingFinished, this,
            [this]() { emit matchTextureChanged(m_matchTextureEdit->text()); });
    connect(m_matchModeCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                if (index < 0)
                {
                    return;
                }
                emit matchTextureModeChanged(static_cast<TextureMatchMode>(m_matchModeCombo->itemData(index).toInt()));
            });

    // ── Separator ──────────────────────────────────────────
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(separator);

    // ── Required Texture Slots ─────────────────────────────
    addSlotRow(PBRTextureSlot::Diffuse, tr("Albedo"), true);
    addSlotRow(PBRTextureSlot::Normal, tr("Normal"), true);

    m_rmaosModeContainer = new QWidget(this);
    auto* rmaosModeLayout = new QHBoxLayout(m_rmaosModeContainer);
    rmaosModeLayout->setContentsMargins(0, 2, 0, 2);
    rmaosModeLayout->setSpacing(10);
    auto* rmaosModeLabel = new QLabel(tr("RMAOS Source:"), m_rmaosModeContainer);
    rmaosModeLabel->setFixedWidth(100);
    m_packedRmaosRadio = new QRadioButton(tr("Packed RMAOS"), m_rmaosModeContainer);
    m_splitRmaosRadio = new QRadioButton(tr("Split Channels"), m_rmaosModeContainer);
    m_packedRmaosRadio->setChecked(true);
    rmaosModeLayout->addWidget(rmaosModeLabel);
    rmaosModeLayout->addWidget(m_packedRmaosRadio);
    rmaosModeLayout->addWidget(m_splitRmaosRadio);
    rmaosModeLayout->addStretch();

    connect(m_packedRmaosRadio, &QRadioButton::toggled, this,
            [this](bool checked)
            {
                if (!checked)
                {
                    return;
                }
                updateRmaosModeUI(RMAOSSourceMode::PackedTexture);
                emit rmaosSourceModeChanged(RMAOSSourceMode::PackedTexture);
            });

    connect(m_splitRmaosRadio, &QRadioButton::toggled, this,
            [this](bool checked)
            {
                if (!checked)
                {
                    return;
                }
                updateRmaosModeUI(RMAOSSourceMode::SeparateChannels);
                emit rmaosSourceModeChanged(RMAOSSourceMode::SeparateChannels);
            });

    addSlotRow(PBRTextureSlot::RMAOS, tr("RMAOS"), true);

    // ── RMAOS Channel Import Section ───────────────────────
    m_channelSection = new QGroupBox(tr("RMAOS Individual Channels (optional)"), this);
    auto* channelLayout = new QVBoxLayout(m_channelSection);
    channelLayout->setContentsMargins(8, 8, 8, 8);
    channelLayout->setSpacing(2);

    addChannelRow(ChannelMap::Roughness, tr("Roughness (R)"));
    addChannelRow(ChannelMap::Metallic, tr("Metallic (G)"));
    addChannelRow(ChannelMap::AO, tr("AO (B)"));
    addChannelRow(ChannelMap::Specular, tr("Specular (A)"));

    for (auto& [ch, row] : m_channelRows)
    {
        channelLayout->addWidget(row.container);
    }

    // ── Optional Texture Slots ─────────────────────────────
    addSlotRow(PBRTextureSlot::Emissive, tr("Emissive"), false);
    addSlotRow(PBRTextureSlot::Displacement, tr("Displacement"), false);
    addSlotRow(PBRTextureSlot::Subsurface, tr("Subsurface"), false);
    addSlotRow(PBRTextureSlot::CoatNormalRoughness, tr("Coat Normal+Rough"), false);
    addSlotRow(PBRTextureSlot::Fuzz, tr("Fuzz"), false);
    addSlotRow(PBRTextureSlot::CoatColor, tr("Coat Color+Strength"), false);

    // Add all slot rows to layout
    for (auto& [slot, row] : m_slotRows)
    {
        if (slot == PBRTextureSlot::RMAOS)
        {
            mainLayout->addWidget(m_rmaosModeContainer);
        }
        mainLayout->addWidget(row.container);
        if (slot == PBRTextureSlot::RMAOS)
        {
            mainLayout->addWidget(m_channelSection);
        }
    }

    // ── Landscape EDID Section ─────────────────────────────
    m_landscapeSection = new QGroupBox(tr("Landscape EDIDs (optional)"), this);
    auto* landscapeLayout = new QVBoxLayout(m_landscapeSection);
    landscapeLayout->setContentsMargins(8, 8, 8, 8);

    auto* landscapeHint = new QLabel(tr("One TXST EDID per line (e.g. LandscapeDirt02).\n"
                                        "Leave empty if this texture set is not used for landscape."),
                                     m_landscapeSection);
    landscapeHint->setWordWrap(true);
    landscapeHint->setStyleSheet("QLabel { color: #999; font-size: 11px; }");
    landscapeLayout->addWidget(landscapeHint);

    m_landscapeEdidEdit = new QPlainTextEdit(m_landscapeSection);
    m_landscapeEdidEdit->setMaximumHeight(80);
    m_landscapeEdidEdit->setPlaceholderText(tr("LandscapeDirt02\nLandscapeGrass01"));
    landscapeLayout->addWidget(m_landscapeEdidEdit);

    connect(m_landscapeEdidEdit, &QPlainTextEdit::textChanged, this,
            [this]()
            {
                QStringList edids;
                for (const auto& line : m_landscapeEdidEdit->toPlainText().split('\n'))
                {
                    auto trimmed = line.trimmed();
                    if (!trimmed.isEmpty())
                        edids.append(trimmed);
                }
                emit landscapeEdidsChanged(edids);
            });

    mainLayout->addWidget(m_landscapeSection);

    mainLayout->addStretch();

    updateRmaosModeUI(RMAOSSourceMode::PackedTexture);
}

void SlotEditorWidget::addSlotRow(PBRTextureSlot slot, const QString& label, bool visible)
{
    SlotRow row;
    row.container = new QWidget(this);
    auto* layout = new QHBoxLayout(row.container);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(6);

    row.labelWidget = new QLabel(label, row.container);
    row.labelWidget->setFixedWidth(100);

    row.dropZone = new DropZoneLabel(row.container);
    row.importButton = new QPushButton(tr("Import"), row.container);
    row.importButton->setFixedWidth(60);
    row.clearButton = new QPushButton(tr("Clear"), row.container);
    row.clearButton->setFixedWidth(50);
    row.clearButton->setToolTip(tr("Remove this texture (revert to default)"));
    row.compressionCombo = new QComboBox(row.container);
    row.compressionCombo->setMinimumWidth(130);
    row.compressionCombo->setToolTip(tr("Select the DDS compression used during export"));
    populateCompressionCombo(row.compressionCombo, slot);

    layout->addWidget(row.labelWidget);
    layout->addWidget(row.dropZone, 1);
    layout->addWidget(row.importButton);
    layout->addWidget(row.clearButton);
    layout->addWidget(row.compressionCombo);

    row.container->setVisible(visible);

    connect(row.importButton, &QPushButton::clicked, this, [this, slot]() { emit importRequested(slot); });

    connect(row.clearButton, &QPushButton::clicked, this, [this, slot]() { emit slotCleared(slot); });

    connect(row.dropZone, &DropZoneLabel::clicked, this, [this, slot]() { emit slotPreviewRequested(slot); });

    connect(row.dropZone, &DropZoneLabel::fileDropped, this,
            [this, slot](const QString& path) { emit fileDroppedOnSlot(slot, path); });

    connect(row.compressionCombo, &QComboBox::currentIndexChanged, this,
            [this, slot, combo = row.compressionCombo](int index)
            {
                if (index < 0)
                {
                    return;
                }

                const auto mode = static_cast<DDSCompressionMode>(combo->itemData(index).toInt());
                emit exportCompressionChanged(slot, mode);
            });

    m_slotRows[slot] = row;
}

void SlotEditorWidget::addChannelRow(ChannelMap channel, const QString& label)
{
    ChannelRowData row;
    row.container = new QWidget(this);
    auto* layout = new QHBoxLayout(row.container);
    layout->setContentsMargins(0, 1, 0, 1);

    row.labelWidget = new QLabel(label, row.container);
    row.labelWidget->setFixedWidth(100);

    row.dropZone = new DropZoneLabel(row.container);
    row.importButton = new QPushButton(tr("Import"), row.container);
    row.importButton->setFixedWidth(60);
    row.clearButton = new QPushButton(tr("Clear"), row.container);
    row.clearButton->setFixedWidth(50);
    row.clearButton->setToolTip(tr("Remove this channel map"));

    layout->addWidget(row.labelWidget);
    layout->addWidget(row.dropZone, 1);
    layout->addWidget(row.importButton);
    layout->addWidget(row.clearButton);

    connect(row.importButton, &QPushButton::clicked, this, [this, channel]() { emit importChannelRequested(channel); });

    connect(row.dropZone, &DropZoneLabel::clicked, this, [this, channel]() { emit channelPreviewRequested(channel); });

    connect(row.clearButton, &QPushButton::clicked, this, [this, channel]() { emit channelCleared(channel); });

    connect(row.dropZone, &DropZoneLabel::fileDropped, this,
            [this, channel](const QString& path) { emit fileDroppedOnChannel(channel, path); });

    m_channelRows[channel] = row;
}

void SlotEditorWidget::updateSlots(const PBRFeatureFlags& features)
{
    auto show = [&](PBRTextureSlot slot, bool vis)
    {
        if (m_slotRows.count(slot))
            m_slotRows[slot].container->setVisible(vis);
    };

    show(PBRTextureSlot::Emissive, features.emissive);
    show(PBRTextureSlot::Displacement, features.parallax || features.coatParallax);
    show(PBRTextureSlot::Subsurface, features.subsurface || features.subsurfaceFoliage);
    show(PBRTextureSlot::CoatNormalRoughness, features.coatNormal);
    show(PBRTextureSlot::Fuzz, features.fuzz);
    show(PBRTextureSlot::CoatColor, features.coatDiffuse);
}

void SlotEditorWidget::updateRmaosModeUI(RMAOSSourceMode mode)
{
    if (m_slotRows.count(PBRTextureSlot::RMAOS))
    {
        auto& row = m_slotRows[PBRTextureSlot::RMAOS];
        row.container->setVisible(true);
        row.dropZone->setVisible(mode == RMAOSSourceMode::PackedTexture);
    }
    if (m_channelSection != nullptr)
    {
        m_channelSection->setVisible(mode == RMAOSSourceMode::SeparateChannels);
    }
}

void SlotEditorWidget::setTextureSet(const PBRTextureSet& ts)
{
    m_matchTextureEdit->setText(QString::fromStdString(ts.matchTexture));
    m_matchModeCombo->blockSignals(true);
    const int matchModeIndex = m_matchModeCombo->findData(static_cast<int>(ts.matchMode));
    m_matchModeCombo->setCurrentIndex(matchModeIndex >= 0 ? matchModeIndex : 0);
    m_matchModeCombo->blockSignals(false);

    m_packedRmaosRadio->blockSignals(true);
    m_splitRmaosRadio->blockSignals(true);
    m_packedRmaosRadio->setChecked(ts.rmaosSourceMode == RMAOSSourceMode::PackedTexture);
    m_splitRmaosRadio->setChecked(ts.rmaosSourceMode == RMAOSSourceMode::SeparateChannels);
    m_packedRmaosRadio->blockSignals(false);
    m_splitRmaosRadio->blockSignals(false);
    updateRmaosModeUI(ts.rmaosSourceMode);

    // Update slot rows
    for (auto& [slot, row] : m_slotRows)
    {
        auto textureIt = ts.textures.find(slot);
        const TextureEntry* entry = textureIt != ts.textures.end() ? &textureIt->second : nullptr;
        const auto allowedModes = allowedCompressionModesForTexture(slot, entry, &ts);

        row.compressionCombo->blockSignals(true);
        row.compressionCombo->clear();
        for (DDSCompressionMode allowedMode : allowedModes)
        {
            row.compressionCombo->addItem(QString::fromUtf8(compressionModeDisplayName(allowedMode)),
                                          static_cast<int>(allowedMode));
        }

        const auto modeIt = ts.exportCompression.find(slot);
        const auto mode = modeIt != ts.exportCompression.end() ? modeIt->second : defaultCompressionForSlot(slot);
        const int comboIndex = row.compressionCombo->findData(static_cast<int>(mode));
        if (comboIndex >= 0)
        {
            row.compressionCombo->setCurrentIndex(comboIndex);
        }
        else
        {
            const int defaultIndex = row.compressionCombo->findData(static_cast<int>(defaultCompressionForSlot(slot)));
            row.compressionCombo->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
        }
        row.compressionCombo->blockSignals(false);

        if (comboIndex < 0 && row.compressionCombo->currentIndex() >= 0)
        {
            const auto fallbackMode = static_cast<DDSCompressionMode>(row.compressionCombo->currentData().toInt());
            emit exportCompressionChanged(slot, fallbackMode);
        }

        if (textureIt != ts.textures.end() && !textureIt->second.sourcePath.empty())
        {
            QString detailText;
            if (textureIt->second.width > 0 && textureIt->second.height > 0)
            {
                detailText = tr("%1 x %2").arg(textureIt->second.width).arg(textureIt->second.height);
            }
            row.dropZone->setFile(textureIt->second.sourcePath, detailText);
        }
        else
        {
            row.dropZone->clear();
        }
    }

    // Update channel rows
    for (auto& [ch, row] : m_channelRows)
    {
        auto it = ts.channelMaps.find(ch);
        if (it != ts.channelMaps.end() && !it->second.sourcePath.empty())
        {
            QString detailText;
            if (it->second.width > 0 && it->second.height > 0)
            {
                detailText = tr("%1 x %2").arg(it->second.width).arg(it->second.height);
            }
            row.dropZone->setFile(it->second.sourcePath, detailText);
        }
        else
        {
            row.dropZone->clear();
        }
    }

    // Update landscape EDIDs
    {
        m_landscapeEdidEdit->blockSignals(true);
        QStringList edids;
        for (const auto& edid : ts.landscapeEdids)
        {
            edids.append(QString::fromStdString(edid));
        }
        m_landscapeEdidEdit->setPlainText(edids.join('\n'));
        m_landscapeEdidEdit->blockSignals(false);
    }

    updateSlots(ts.features);
}

} // namespace tpbr
