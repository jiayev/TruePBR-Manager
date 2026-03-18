#include "VanillaConversionDialog.h"

#include "utils/DDSUtils.h"
#include "utils/ImageUtils.h"

#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QScrollArea>
#include <QSplitter>
#include <QPixmap>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tpbr
{

// ─── Static helpers ───────────────────────────────────────────

QString VanillaConversionDialog::textureTypeDisplayName(VanillaTextureType type)
{
    switch (type)
    {
    case VanillaTextureType::Diffuse:
        return tr("Diffuse (Required)");
    case VanillaTextureType::Normal:
        return tr("Normal (Required)");
    case VanillaTextureType::Glow:
        return tr("Glow");
    case VanillaTextureType::Parallax:
        return tr("Parallax");
    case VanillaTextureType::Specular:
        return tr("Specular");
    case VanillaTextureType::BackLight:
        return tr("BackLight");
    case VanillaTextureType::EnvMask:
        return tr("EnvMask");
    case VanillaTextureType::Cubemap:
        return tr("Cubemap");
    }
    return {};
}

QString VanillaConversionDialog::outputSlotDisplayName(PBROutputSlot slot)
{
    switch (slot)
    {
    case PBROutputSlot::Albedo:
        return tr("Albedo");
    case PBROutputSlot::Normal:
        return tr("Normal");
    case PBROutputSlot::RMAOS:
        return tr("RMAOS");
    case PBROutputSlot::Emissive:
        return tr("Emissive");
    case PBROutputSlot::Displacement:
        return tr("Displacement");
    case PBROutputSlot::Subsurface:
        return tr("Subsurface");
    }
    return {};
}

QString VanillaConversionDialog::imageFileFilter()
{
    return tr("Image Files (*.png *.dds *.tga *.bmp *.jpg *.jpeg);;All Files (*)");
}

bool VanillaConversionDialog::isColorTexture(VanillaTextureType type)
{
    return type == VanillaTextureType::Diffuse || type == VanillaTextureType::Glow ||
           type == VanillaTextureType::BackLight || type == VanillaTextureType::Cubemap;
}

QPixmap VanillaConversionDialog::generateThumbnail(const uint8_t* rgba, int width, int height, int thumbnailSize)
{
    QImage image(rgba, width, height, width * 4, QImage::Format_RGBA8888);
    QPixmap pixmap = QPixmap::fromImage(image);
    return pixmap.scaled(thumbnailSize, thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

std::vector<uint8_t> VanillaConversionDialog::downscaleForPreview(const uint8_t* rgba, int srcW, int srcH, int maxSize,
                                                                  int& outW, int& outH)
{
    // If already small enough, just copy
    if (srcW <= maxSize && srcH <= maxSize)
    {
        outW = srcW;
        outH = srcH;
        return std::vector<uint8_t>(rgba, rgba + static_cast<size_t>(srcW) * srcH * 4);
    }

    // Use QImage for high-quality downscale (bilinear)
    QImage srcImage(rgba, srcW, srcH, srcW * 4, QImage::Format_RGBA8888);
    QImage scaled = srcImage.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGBA8888);
    outW = scaled.width();
    outH = scaled.height();

    // Copy pixel data out (QImage data may not be contiguous per-row due to padding)
    std::vector<uint8_t> result(static_cast<size_t>(outW) * outH * 4);
    for (int y = 0; y < outH; ++y)
    {
        const uint8_t* scanline = scaled.constScanLine(y);
        std::memcpy(result.data() + y * outW * 4, scanline, static_cast<size_t>(outW) * 4);
    }
    return result;
}

// ─── Constructor ──────────────────────────────────────────────

VanillaConversionDialog::VanillaConversionDialog(QWidget* parent) : QDialog(parent)
{
    setupUi();
}

// ─── UI construction ──────────────────────────────────────────

void VanillaConversionDialog::setupUi()
{
    setWindowTitle(tr("Convert Vanilla Textures"));
    setMinimumSize(1100, 750);

    auto* mainLayout = new QVBoxLayout(this);

    // ═══════════════════════════════════════════════════════════
    // TOP: Integrated flow layout (Input → Output)
    // ═══════════════════════════════════════════════════════════
    auto* flowSplitter = new QSplitter(Qt::Horizontal, this);

    // ── LEFT: Input Textures ───────────────────────────────────
    inputGroup_ = new QGroupBox(tr("Vanilla Input Textures"), this);
    auto* inputOuterLayout = new QVBoxLayout(inputGroup_);

    auto* inputScroll = new QScrollArea(this);
    inputScroll->setWidgetResizable(true);
    inputScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inputScrollContent = new QWidget();
    auto* inputLayout = new QVBoxLayout(inputScrollContent);
    inputLayout->setSpacing(6);

    static constexpr VanillaTextureType kTextureTypes[] = {
        VanillaTextureType::Diffuse,  VanillaTextureType::Normal,   VanillaTextureType::Glow,
        VanillaTextureType::Parallax, VanillaTextureType::Specular, VanillaTextureType::BackLight,
        VanillaTextureType::EnvMask,  VanillaTextureType::Cubemap,
    };

    for (VanillaTextureType type : kTextureTypes)
    {
        InputZone zone;

        // Row container
        zone.container = new QWidget(inputScrollContent);
        auto* rowLayout = new QHBoxLayout(zone.container);
        rowLayout->setContentsMargins(4, 2, 4, 2);

        // Thumbnail preview
        zone.thumbnailLabel = new QLabel(zone.container);
        zone.thumbnailLabel->setFixedSize(kInputThumbnailSize, kInputThumbnailSize);
        zone.thumbnailLabel->setAlignment(Qt::AlignCenter);
        zone.thumbnailLabel->setStyleSheet(
            QStringLiteral("background-color: #2a2a2a; border: 1px solid #555; border-radius: 3px;"));
        rowLayout->addWidget(zone.thumbnailLabel);

        // Info column: type name + path + browse/clear
        auto* infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(2);

        // First row: type label + browse + clear
        auto* topRow = new QHBoxLayout();
        zone.typeLabel = new QLabel(textureTypeDisplayName(type), zone.container);
        zone.typeLabel->setMinimumWidth(120);
        QFont boldFont = zone.typeLabel->font();
        boldFont.setBold(true);
        zone.typeLabel->setFont(boldFont);
        topRow->addWidget(zone.typeLabel);

        zone.browseButton = new QPushButton(tr("Browse..."), zone.container);
        zone.browseButton->setFixedWidth(80);
        topRow->addWidget(zone.browseButton);

        zone.clearButton = new QPushButton(tr("Clear"), zone.container);
        zone.clearButton->setFixedWidth(50);
        zone.clearButton->setEnabled(false);
        topRow->addWidget(zone.clearButton);
        topRow->addStretch();
        infoLayout->addLayout(topRow);

        // Second row: path label
        zone.pathLabel = new QLabel(tr("Not set"), zone.container);
        zone.pathLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
        zone.pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoLayout->addWidget(zone.pathLabel);

        // Third row: per-color-texture gamma/brightness (only for color textures)
        if (isColorTexture(type))
        {
            auto* gbRow = new QHBoxLayout();
            gbRow->setSpacing(8);

            zone.gammaLabel = new QLabel(tr("Gamma:"), zone.container);
            zone.gammaLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
            gbRow->addWidget(zone.gammaLabel);

            zone.gammaSpin = new QDoubleSpinBox(zone.container);
            zone.gammaSpin->setRange(0.1, 5.0);
            zone.gammaSpin->setValue(1.0);
            zone.gammaSpin->setDecimals(2);
            zone.gammaSpin->setSingleStep(0.01);
            zone.gammaSpin->setFixedWidth(70);
            gbRow->addWidget(zone.gammaSpin);

            zone.brightnessLabel = new QLabel(tr("Brightness:"), zone.container);
            zone.brightnessLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
            gbRow->addWidget(zone.brightnessLabel);

            zone.brightnessSpin = new QDoubleSpinBox(zone.container);
            zone.brightnessSpin->setRange(-1.0, 1.0);
            zone.brightnessSpin->setValue(0.0);
            zone.brightnessSpin->setDecimals(2);
            zone.brightnessSpin->setSingleStep(0.01);
            zone.brightnessSpin->setFixedWidth(70);
            gbRow->addWidget(zone.brightnessSpin);

            gbRow->addStretch();
            infoLayout->addLayout(gbRow);

            // Connect gamma/brightness to debounced preview update
            connect(zone.gammaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    &VanillaConversionDialog::onParameterChanged);
            connect(zone.brightnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    &VanillaConversionDialog::onParameterChanged);
        }

        rowLayout->addLayout(infoLayout, 1);

        inputLayout->addWidget(zone.container);

        // Connect browse/clear
        connect(zone.browseButton, &QPushButton::clicked, this, [this, type]() { onBrowseInput(type); });
        connect(zone.clearButton, &QPushButton::clicked, this, [this, type]() { onClearInput(type); });

        inputZones_[type] = zone;
    }

    inputLayout->addStretch();
    inputScroll->setWidget(inputScrollContent);
    inputOuterLayout->addWidget(inputScroll);
    flowSplitter->addWidget(inputGroup_);

    // ── RIGHT: PBR Output Previews (all shown simultaneously) ──
    outputPreviewGroup_ = new QGroupBox(tr("PBR Output Previews"), this);
    auto* outputLayout = new QGridLayout(outputPreviewGroup_);
    outputLayout->setSpacing(8);

    static constexpr PBROutputSlot kOutputSlots[] = {
        PBROutputSlot::Albedo,   PBROutputSlot::Normal,       PBROutputSlot::RMAOS,
        PBROutputSlot::Emissive, PBROutputSlot::Displacement, PBROutputSlot::Subsurface,
    };

    int outIdx = 0;
    for (PBROutputSlot slot : kOutputSlots)
    {
        OutputPreview preview;

        preview.container = new QWidget(outputPreviewGroup_);
        auto* pLayout = new QVBoxLayout(preview.container);
        pLayout->setContentsMargins(2, 2, 2, 2);
        pLayout->setSpacing(2);

        // Name label above
        preview.nameLabel = new QLabel(outputSlotDisplayName(slot), preview.container);
        preview.nameLabel->setAlignment(Qt::AlignCenter);
        QFont nameFont = preview.nameLabel->font();
        nameFont.setBold(true);
        preview.nameLabel->setFont(nameFont);
        pLayout->addWidget(preview.nameLabel);

        // Preview thumbnail
        preview.thumbnailLabel = new QLabel(preview.container);
        preview.thumbnailLabel->setFixedSize(kOutputThumbnailSize, kOutputThumbnailSize);
        preview.thumbnailLabel->setAlignment(Qt::AlignCenter);
        preview.thumbnailLabel->setStyleSheet(
            QStringLiteral("background-color: #1e1e1e; border: 1px solid #444; border-radius: 3px;"));
        preview.thumbnailLabel->setText(QStringLiteral("--"));
        pLayout->addWidget(preview.thumbnailLabel, 0, Qt::AlignCenter);

        // Grid: 3 columns, 2 rows
        int row = outIdx / 3;
        int col = outIdx % 3;
        outputLayout->addWidget(preview.container, row, col);

        outputPreviews_[slot] = preview;
        ++outIdx;
    }

    flowSplitter->addWidget(outputPreviewGroup_);

    // Set splitter proportions (40% input, 60% output)
    flowSplitter->setStretchFactor(0, 2);
    flowSplitter->setStretchFactor(1, 3);

    mainLayout->addWidget(flowSplitter, 1);

    // ═══════════════════════════════════════════════════════════
    // BOTTOM: Parameters + Output Settings + Buttons
    // ═══════════════════════════════════════════════════════════
    auto* bottomLayout = new QHBoxLayout();

    // ── Parameters ─────────────────────────────────────────────
    paramGroup_ = new QGroupBox(tr("Conversion Parameters"), this);
    auto* paramLayout = new QFormLayout(paramGroup_);

    shininessSpin_ = new QDoubleSpinBox(this);
    shininessSpin_->setRange(0.1, 10000.0);
    shininessSpin_->setValue(50.0);
    shininessSpin_->setDecimals(1);
    shininessLabel_ = new QLabel(tr("Shininess:"), this);
    paramLayout->addRow(shininessLabel_, shininessSpin_);

    specularModeCombo_ = new QComboBox(this);
    specularModeCombo_->addItem(tr("Direct"), static_cast<int>(SpecularMode::Direct));
    specularModeCombo_->addItem(tr("Divide by PI"), static_cast<int>(SpecularMode::DividePI));
    specularModeLabel_ = new QLabel(tr("Specular Mode:"), this);
    paramLayout->addRow(specularModeLabel_, specularModeCombo_);

    normalAlphaCheck_ = new QCheckBox(tr("Normal Alpha is Specular"), this);
    paramLayout->addRow(normalAlphaCheck_);

    // Metallic roughness override: checkbox + spinbox on same row
    auto* metalRoughRow = new QHBoxLayout();
    metallicRoughnessCheck_ = new QCheckBox(tr("Metallic Roughness Override:"), this);
    metalRoughRow->addWidget(metallicRoughnessCheck_);
    metallicRoughnessSpin_ = new QDoubleSpinBox(this);
    metallicRoughnessSpin_->setRange(0.0, 1.0);
    metallicRoughnessSpin_->setValue(0.3);
    metallicRoughnessSpin_->setDecimals(2);
    metallicRoughnessSpin_->setSingleStep(0.01);
    metallicRoughnessSpin_->setEnabled(false);
    metalRoughRow->addWidget(metallicRoughnessSpin_);
    metalRoughRow->addStretch();
    paramLayout->addRow(metalRoughRow);

    connect(metallicRoughnessCheck_, &QCheckBox::toggled, metallicRoughnessSpin_, &QDoubleSpinBox::setEnabled);

    bottomLayout->addWidget(paramGroup_);

    // ── Output Settings ────────────────────────────────────────
    outputSettingsGroup_ = new QGroupBox(tr("Output"), this);
    auto* outputSettingsLayout = new QFormLayout(outputSettingsGroup_);

    auto* dirRowLayout = new QHBoxLayout();
    outputDirEdit_ = new QLineEdit(this);
    outputDirBrowseBtn_ = new QPushButton(tr("Browse..."), this);
    dirRowLayout->addWidget(outputDirEdit_);
    dirRowLayout->addWidget(outputDirBrowseBtn_);
    outputDirLabel_ = new QLabel(tr("Output Directory:"), this);
    outputSettingsLayout->addRow(outputDirLabel_, dirRowLayout);

    textureSetNameEdit_ = new QLineEdit(this);
    textureSetNameLabel_ = new QLabel(tr("Texture Set Name:"), this);
    outputSettingsLayout->addRow(textureSetNameLabel_, textureSetNameEdit_);

    vanillaMatchPathEdit_ = new QLineEdit(this);
    vanillaMatchPathEdit_->setPlaceholderText(tr("e.g. architecture\\whiterun\\wrwoodplank01"));
    vanillaMatchPathLabel_ = new QLabel(tr("Vanilla Match Path:"), this);
    outputSettingsLayout->addRow(vanillaMatchPathLabel_, vanillaMatchPathEdit_);

    bottomLayout->addWidget(outputSettingsGroup_, 1);

    mainLayout->addLayout(bottomLayout);

    // ── Button row ─────────────────────────────────────────────
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    convertButton_ = new QPushButton(tr("Convert"), this);
    convertButton_->setDefault(true);
    convertButton_->setEnabled(false);

    cancelButton_ = new QPushButton(tr("Cancel"), this);

    buttonLayout->addWidget(convertButton_);
    buttonLayout->addWidget(cancelButton_);
    mainLayout->addLayout(buttonLayout);

    // ── Signals ────────────────────────────────────────────────
    connect(outputDirBrowseBtn_, &QPushButton::clicked, this, &VanillaConversionDialog::onBrowseOutputDir);
    connect(convertButton_, &QPushButton::clicked, this, &VanillaConversionDialog::onConvert);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);

    // ── Preview debounce timer ─────────────────────────────────
    previewDebounceTimer_ = new QTimer(this);
    previewDebounceTimer_->setSingleShot(true);
    previewDebounceTimer_->setInterval(150); // 150ms debounce
    connect(previewDebounceTimer_, &QTimer::timeout, this, &VanillaConversionDialog::updateAllPreviews);

    // ── Parameter changes → debounced preview update ───────────
    connect(shininessSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VanillaConversionDialog::onParameterChanged);
    connect(specularModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &VanillaConversionDialog::onParameterChanged);
    connect(normalAlphaCheck_, &QCheckBox::toggled, this, &VanillaConversionDialog::onParameterChanged);
    connect(metallicRoughnessCheck_, &QCheckBox::toggled, this, &VanillaConversionDialog::onParameterChanged);
    connect(metallicRoughnessSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VanillaConversionDialog::onParameterChanged);
}

// ─── Slots ─────────────────────────────────────────────────────

void VanillaConversionDialog::onBrowseInput(VanillaTextureType type)
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Select %1 Texture").arg(textureTypeDisplayName(type)), QString(), imageFileFilter());
    if (filePath.isEmpty())
        return;

    auto it = inputZones_.find(type);
    if (it == inputZones_.end())
        return;

    auto& zone = it->second;
    zone.path = filePath.toStdString();

    // Show just the filename, full path in tooltip
    const QString filename = QFileInfo(filePath).fileName();
    zone.pathLabel->setText(filename);
    zone.pathLabel->setToolTip(filePath);
    zone.pathLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    zone.clearButton->setEnabled(true);

    // Load pixels and update input thumbnail
    loadTexturePixels(type);
    updateInputThumbnail(type);

    updateConvertButtonState();

    // Trigger output preview update
    onParameterChanged();
}

void VanillaConversionDialog::onClearInput(VanillaTextureType type)
{
    auto it = inputZones_.find(type);
    if (it == inputZones_.end())
        return;

    auto& zone = it->second;
    zone.path.clear();
    zone.previewPixels.clear();
    zone.previewWidth = 0;
    zone.previewHeight = 0;
    zone.fullWidth = 0;
    zone.fullHeight = 0;
    zone.pathLabel->setText(tr("Not set"));
    zone.pathLabel->setToolTip(QString());
    zone.pathLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    zone.clearButton->setEnabled(false);
    zone.thumbnailLabel->setPixmap(QPixmap());

    updateConvertButtonState();
    onParameterChanged();
}

void VanillaConversionDialog::onBrowseOutputDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"), outputDirEdit_->text());
    if (!dir.isEmpty())
        outputDirEdit_->setText(dir);
}

void VanillaConversionDialog::onConvert()
{
    accepted_ = true;
    accept();
}

void VanillaConversionDialog::updateConvertButtonState()
{
    const bool hasDiffuse =
        inputZones_.count(VanillaTextureType::Diffuse) && !inputZones_.at(VanillaTextureType::Diffuse).path.empty();
    const bool hasNormal =
        inputZones_.count(VanillaTextureType::Normal) && !inputZones_.at(VanillaTextureType::Normal).path.empty();

    convertButton_->setEnabled(hasDiffuse && hasNormal);
}

// ─── Preview ───────────────────────────────────────────────────

void VanillaConversionDialog::onParameterChanged()
{
    // Restart debounce timer
    previewDebounceTimer_->start();
}

void VanillaConversionDialog::updateAllPreviews()
{
    generateOutputPreviews();
}

bool VanillaConversionDialog::loadTexturePixels(VanillaTextureType type)
{
    auto it = inputZones_.find(type);
    if (it == inputZones_.end())
        return false;

    auto& zone = it->second;
    if (zone.path.empty())
        return false;

    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    int channels = 4;

    // Try DDS first
    const auto ext = zone.path.extension().string();
    bool loaded = false;

    if (ext == ".dds" || ext == ".DDS")
    {
        loaded = DDSUtils::loadDDS(zone.path, width, height, pixels);
    }

    if (!loaded)
    {
        auto imageData = ImageUtils::loadImage(zone.path);
        if (imageData.width > 0 && imageData.height > 0 && !imageData.pixels.empty())
        {
            width = imageData.width;
            height = imageData.height;
            channels = imageData.channels;
            pixels = std::move(imageData.pixels);
            loaded = true;
        }
    }

    if (!loaded || pixels.empty())
        return false;

    // Ensure RGBA format
    if (channels == 3)
    {
        std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
        for (int i = 0; i < width * height; ++i)
        {
            rgba[i * 4 + 0] = pixels[i * 3 + 0];
            rgba[i * 4 + 1] = pixels[i * 3 + 1];
            rgba[i * 4 + 2] = pixels[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
        pixels = std::move(rgba);
    }

    // Store full-resolution dimensions (pixels are NOT kept in memory)
    zone.fullWidth = width;
    zone.fullHeight = height;

    // Downscale to preview working size — all real-time preview math runs on this
    zone.previewPixels =
        downscaleForPreview(pixels.data(), width, height, kPreviewWorkingSize, zone.previewWidth, zone.previewHeight);

    return true;
}

void VanillaConversionDialog::updateInputThumbnail(VanillaTextureType type)
{
    auto it = inputZones_.find(type);
    if (it == inputZones_.end())
        return;

    auto& zone = it->second;
    if (zone.previewPixels.empty())
    {
        zone.thumbnailLabel->setPixmap(QPixmap());
        return;
    }

    // For color textures, apply gamma/brightness on the small preview copy
    if (isColorTexture(type) && zone.gammaSpin && zone.brightnessSpin)
    {
        const float gamma = static_cast<float>(zone.gammaSpin->value());
        const float brightness = static_cast<float>(zone.brightnessSpin->value());

        if (gamma != 1.0f || brightness != 0.0f)
        {
            std::vector<uint8_t> adjusted = zone.previewPixels;
            VanillaConverter::applyGammaBrightness(adjusted.data(), zone.previewWidth, zone.previewHeight, gamma,
                                                   brightness);
            zone.thumbnailLabel->setPixmap(
                generateThumbnail(adjusted.data(), zone.previewWidth, zone.previewHeight, kInputThumbnailSize));
            return;
        }
    }

    zone.thumbnailLabel->setPixmap(
        generateThumbnail(zone.previewPixels.data(), zone.previewWidth, zone.previewHeight, kInputThumbnailSize));
}

void VanillaConversionDialog::generateOutputPreviews()
{
    // Gather which inputs are available (check preview pixels, not full-res)
    auto hasInput = [&](VanillaTextureType type) -> bool
    {
        auto it = inputZones_.find(type);
        return it != inputZones_.end() && !it->second.previewPixels.empty();
    };

    auto getZone = [&](VanillaTextureType type) -> const InputZone*
    {
        auto it = inputZones_.find(type);
        if (it != inputZones_.end() && !it->second.previewPixels.empty())
            return &it->second;
        return nullptr;
    };

    const bool hasDiffuse = hasInput(VanillaTextureType::Diffuse);
    const bool hasNormal = hasInput(VanillaTextureType::Normal);
    const bool hasGlow = hasInput(VanillaTextureType::Glow);
    const bool hasParallax = hasInput(VanillaTextureType::Parallax);
    const bool hasSpecular = hasInput(VanillaTextureType::Specular);
    const bool hasBackLight = hasInput(VanillaTextureType::BackLight);
    const bool hasEnvMask = hasInput(VanillaTextureType::EnvMask);

    // Also update input thumbnails for color textures (they may have gamma/brightness)
    for (VanillaTextureType type : {VanillaTextureType::Diffuse, VanillaTextureType::Glow,
                                    VanillaTextureType::BackLight, VanillaTextureType::Cubemap})
    {
        if (hasInput(type))
            updateInputThumbnail(type);
    }

    // ── Albedo output ──────────────────────────────────────────
    {
        auto& preview = outputPreviews_[PBROutputSlot::Albedo];
        if (hasDiffuse)
        {
            const auto* zone = getZone(VanillaTextureType::Diffuse);
            // Apply per-texture gamma/brightness on the small preview copy
            std::vector<uint8_t> albedoPixels = zone->previewPixels;
            if (!zone->previewPixels.empty() && inputZones_[VanillaTextureType::Diffuse].gammaSpin)
            {
                const float gamma = static_cast<float>(inputZones_[VanillaTextureType::Diffuse].gammaSpin->value());
                const float brightness =
                    static_cast<float>(inputZones_[VanillaTextureType::Diffuse].brightnessSpin->value());
                if (gamma != 1.0f || brightness != 0.0f)
                {
                    VanillaConverter::applyGammaBrightness(albedoPixels.data(), zone->previewWidth, zone->previewHeight,
                                                           gamma, brightness);
                }
            }

            // Apply cubemap metallic tint overlay: albedo = lerp(albedo, albedo * cubemapColor, envMask)
            const bool hasCubemap = hasInput(VanillaTextureType::Cubemap);
            if (hasEnvMask && hasCubemap)
            {
                const auto* envZone = getZone(VanillaTextureType::EnvMask);
                const auto* cubemapZone = getZone(VanillaTextureType::Cubemap);

                if (envZone && cubemapZone && envZone->previewWidth == zone->previewWidth &&
                    envZone->previewHeight == zone->previewHeight)
                {
                    // Compute cubemap average color from preview pixels, with gamma/brightness applied
                    std::vector<uint8_t> cubemapAdj = cubemapZone->previewPixels;
                    if (inputZones_[VanillaTextureType::Cubemap].gammaSpin)
                    {
                        const float cGamma =
                            static_cast<float>(inputZones_[VanillaTextureType::Cubemap].gammaSpin->value());
                        const float cBrightness =
                            static_cast<float>(inputZones_[VanillaTextureType::Cubemap].brightnessSpin->value());
                        if (cGamma != 1.0f || cBrightness != 0.0f)
                            VanillaConverter::applyGammaBrightness(cubemapAdj.data(), cubemapZone->previewWidth,
                                                                   cubemapZone->previewHeight, cGamma, cBrightness);
                    }
                    const int cubeCount = cubemapZone->previewWidth * cubemapZone->previewHeight;
                    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
                    for (int j = 0; j < cubeCount; ++j)
                    {
                        sumR += VanillaConverter::srgbToLinear(cubemapAdj[j * 4 + 0]);
                        sumG += VanillaConverter::srgbToLinear(cubemapAdj[j * 4 + 1]);
                        sumB += VanillaConverter::srgbToLinear(cubemapAdj[j * 4 + 2]);
                    }
                    const float tintR = static_cast<float>(sumR / cubeCount);
                    const float tintG = static_cast<float>(sumG / cubeCount);
                    const float tintB = static_cast<float>(sumB / cubeCount);

                    const int count = zone->previewWidth * zone->previewHeight;
                    for (int i = 0; i < count; ++i)
                    {
                        const float mask = static_cast<float>(envZone->previewPixels[i * 4 + 0]) / 255.0f;
                        if (mask <= 0.0f)
                            continue;

                        for (int c = 0; c < 3; ++c)
                        {
                            float linear = VanillaConverter::srgbToLinear(albedoPixels[i * 4 + c]);
                            float cubeC = (c == 0) ? tintR : (c == 1) ? tintG : tintB;
                            float blended = linear + (cubeC - linear) * mask; // lerp(diffuse, cubemap, envMask)
                            albedoPixels[i * 4 + c] = VanillaConverter::linearToSrgb(blended);
                        }
                    }
                }
            }

            preview.thumbnailLabel->setPixmap(
                generateThumbnail(albedoPixels.data(), zone->previewWidth, zone->previewHeight, kOutputThumbnailSize));
        }
        else
        {
            preview.thumbnailLabel->setPixmap(QPixmap());
            preview.thumbnailLabel->setText(QStringLiteral("--"));
        }
    }

    // ── Normal output ──────────────────────────────────────────
    {
        auto& preview = outputPreviews_[PBROutputSlot::Normal];
        if (hasNormal)
        {
            const auto* zone = getZone(VanillaTextureType::Normal);
            preview.thumbnailLabel->setPixmap(generateThumbnail(zone->previewPixels.data(), zone->previewWidth,
                                                                zone->previewHeight, kOutputThumbnailSize));
        }
        else
        {
            preview.thumbnailLabel->setPixmap(QPixmap());
            preview.thumbnailLabel->setText(QStringLiteral("--"));
        }
    }

    // ── RMAOS output (synthesized preview on small preview data) ─
    {
        auto& preview = outputPreviews_[PBROutputSlot::RMAOS];
        if (hasDiffuse || hasSpecular || hasEnvMask)
        {
            const InputZone* refZone = getZone(VanillaTextureType::Diffuse);
            if (!refZone)
                refZone = getZone(VanillaTextureType::Specular);
            if (!refZone)
                refZone = getZone(VanillaTextureType::EnvMask);

            if (refZone)
            {
                int w = refZone->previewWidth;
                int h = refZone->previewHeight;
                std::vector<uint8_t> rmaos(static_cast<size_t>(w) * h * 4);

                const uint8_t roughness =
                    VanillaConverter::shininessToRoughness(static_cast<float>(shininessSpin_->value()));

                // Metallic roughness override (if enabled)
                const bool hasMetalRoughOverride = metallicRoughnessCheck_->isChecked();
                const uint8_t metalRoughness =
                    hasMetalRoughOverride
                        ? static_cast<uint8_t>(std::clamp(metallicRoughnessSpin_->value() * 255.0, 0.0, 255.0))
                        : roughness;

                const InputZone* specZone = getZone(VanillaTextureType::Specular);
                const InputZone* envZone = getZone(VanillaTextureType::EnvMask);

                const InputZone* normalZone = getZone(VanillaTextureType::Normal);
                bool useNormalAlpha = normalAlphaCheck_->isChecked() && normalZone && !specZone;

                for (int i = 0; i < w * h; ++i)
                {
                    // G = Metallic (from EnvMask red channel)
                    uint8_t metallic = 0;
                    if (envZone && envZone->previewWidth == w && envZone->previewHeight == h)
                        metallic = envZone->previewPixels[i * 4 + 0];

                    // R = Roughness (override for metallic areas)
                    if (metallic > 0 && hasMetalRoughOverride)
                        rmaos[i * 4 + 0] = metalRoughness;
                    else
                        rmaos[i * 4 + 0] = roughness;

                    rmaos[i * 4 + 1] = metallic;

                    rmaos[i * 4 + 2] = 255; // B = AO (always white)

                    // A = Specular
                    if (specZone && specZone->previewWidth == w && specZone->previewHeight == h)
                    {
                        rmaos[i * 4 + 3] = specZone->previewPixels[i * 4 + 0];
                    }
                    else if (useNormalAlpha && normalZone->previewWidth == w && normalZone->previewHeight == h)
                    {
                        rmaos[i * 4 + 3] = normalZone->previewPixels[i * 4 + 3];
                    }
                    else
                    {
                        rmaos[i * 4 + 3] = 20; // Default ~0.08 * 255
                    }
                }

                preview.thumbnailLabel->setPixmap(generateThumbnail(rmaos.data(), w, h, kOutputThumbnailSize));
            }
            else
            {
                preview.thumbnailLabel->setPixmap(QPixmap());
                preview.thumbnailLabel->setText(QStringLiteral("--"));
            }
        }
        else
        {
            preview.thumbnailLabel->setPixmap(QPixmap());
            preview.thumbnailLabel->setText(QStringLiteral("--"));
        }
    }

    // ── Emissive output ────────────────────────────────────────
    {
        auto& preview = outputPreviews_[PBROutputSlot::Emissive];
        if (hasGlow)
        {
            const auto* zone = getZone(VanillaTextureType::Glow);
            std::vector<uint8_t> glowPixels = zone->previewPixels;
            if (inputZones_[VanillaTextureType::Glow].gammaSpin)
            {
                const float gamma = static_cast<float>(inputZones_[VanillaTextureType::Glow].gammaSpin->value());
                const float brightness =
                    static_cast<float>(inputZones_[VanillaTextureType::Glow].brightnessSpin->value());
                if (gamma != 1.0f || brightness != 0.0f)
                {
                    VanillaConverter::applyGammaBrightness(glowPixels.data(), zone->previewWidth, zone->previewHeight,
                                                           gamma, brightness);
                }
            }
            preview.thumbnailLabel->setPixmap(
                generateThumbnail(glowPixels.data(), zone->previewWidth, zone->previewHeight, kOutputThumbnailSize));
        }
        else
        {
            preview.thumbnailLabel->setPixmap(QPixmap());
            preview.thumbnailLabel->setText(QStringLiteral("--"));
        }
    }

    // ── Displacement output ────────────────────────────────────
    {
        auto& preview = outputPreviews_[PBROutputSlot::Displacement];
        if (hasParallax)
        {
            const auto* zone = getZone(VanillaTextureType::Parallax);
            preview.thumbnailLabel->setPixmap(generateThumbnail(zone->previewPixels.data(), zone->previewWidth,
                                                                zone->previewHeight, kOutputThumbnailSize));
        }
        else
        {
            preview.thumbnailLabel->setPixmap(QPixmap());
            preview.thumbnailLabel->setText(QStringLiteral("--"));
        }
    }

    // ── Subsurface output ──────────────────────────────────────
    {
        auto& preview = outputPreviews_[PBROutputSlot::Subsurface];
        if (hasBackLight)
        {
            const auto* zone = getZone(VanillaTextureType::BackLight);
            std::vector<uint8_t> blPixels = zone->previewPixels;
            if (inputZones_[VanillaTextureType::BackLight].gammaSpin)
            {
                const float gamma = static_cast<float>(inputZones_[VanillaTextureType::BackLight].gammaSpin->value());
                const float brightness =
                    static_cast<float>(inputZones_[VanillaTextureType::BackLight].brightnessSpin->value());
                if (gamma != 1.0f || brightness != 0.0f)
                {
                    VanillaConverter::applyGammaBrightness(blPixels.data(), zone->previewWidth, zone->previewHeight,
                                                           gamma, brightness);
                }
            }
            preview.thumbnailLabel->setPixmap(
                generateThumbnail(blPixels.data(), zone->previewWidth, zone->previewHeight, kOutputThumbnailSize));
        }
        else
        {
            preview.thumbnailLabel->setPixmap(QPixmap());
            preview.thumbnailLabel->setText(QStringLiteral("--"));
        }
    }
}

// ─── Result extraction ─────────────────────────────────────────

VanillaConversionInput VanillaConversionDialog::getConversionInput() const
{
    VanillaConversionInput input;

    // Collect input files
    for (const auto& [type, zone] : inputZones_)
    {
        if (!zone.path.empty())
            input.inputFiles[type] = zone.path;
    }

    // Parameters
    input.params.shininess = static_cast<float>(shininessSpin_->value());

    // Per-texture gamma/brightness
    for (VanillaTextureType colorType : {VanillaTextureType::Diffuse, VanillaTextureType::Glow,
                                         VanillaTextureType::BackLight, VanillaTextureType::Cubemap})
    {
        auto it = inputZones_.find(colorType);
        if (it != inputZones_.end() && it->second.gammaSpin)
        {
            GammaBrightnessParams gb;
            gb.gamma = static_cast<float>(it->second.gammaSpin->value());
            gb.brightness = static_cast<float>(it->second.brightnessSpin->value());
            if (gb.gamma != 1.0f || gb.brightness != 0.0f)
            {
                input.params.colorAdjustments[colorType] = gb;
            }
        }
    }

    const int specIdx = specularModeCombo_->currentIndex();
    if (specIdx >= 0)
        input.params.specularMode = static_cast<SpecularMode>(specularModeCombo_->itemData(specIdx).toInt());

    input.params.normalAlphaIsSpecular = normalAlphaCheck_->isChecked();

    // Metallic roughness override
    if (metallicRoughnessCheck_->isChecked())
        input.params.metallicRoughnessOverride = static_cast<float>(metallicRoughnessSpin_->value());

    // Output
    input.outputDir = outputDirEdit_->text().toStdString();
    input.textureSetName = textureSetNameEdit_->text().toStdString();
    input.vanillaMatchPath = vanillaMatchPathEdit_->text().toStdString();

    return input;
}

// ─── i18n ──────────────────────────────────────────────────────

void VanillaConversionDialog::retranslateUi()
{
    setWindowTitle(tr("Convert Vanilla Textures"));

    inputGroup_->setTitle(tr("Vanilla Input Textures"));
    outputPreviewGroup_->setTitle(tr("PBR Output Previews"));
    paramGroup_->setTitle(tr("Conversion Parameters"));
    outputSettingsGroup_->setTitle(tr("Output"));

    // Input zone labels
    for (auto& [type, zone] : inputZones_)
    {
        zone.typeLabel->setText(textureTypeDisplayName(type));
        zone.browseButton->setText(tr("Browse..."));
        zone.clearButton->setText(tr("Clear"));
        if (zone.path.empty())
            zone.pathLabel->setText(tr("Not set"));

        if (zone.gammaLabel)
            zone.gammaLabel->setText(tr("Gamma:"));
        if (zone.brightnessLabel)
            zone.brightnessLabel->setText(tr("Brightness:"));
    }

    // Output preview labels
    for (auto& [slot, preview] : outputPreviews_)
    {
        preview.nameLabel->setText(outputSlotDisplayName(slot));
    }

    // Parameter labels
    shininessLabel_->setText(tr("Shininess:"));
    specularModeLabel_->setText(tr("Specular Mode:"));
    normalAlphaCheck_->setText(tr("Normal Alpha is Specular"));
    metallicRoughnessCheck_->setText(tr("Metallic Roughness Override:"));

    // Specular mode combo items
    specularModeCombo_->setItemText(0, tr("Direct"));
    specularModeCombo_->setItemText(1, tr("Divide by PI"));

    // Output labels
    outputDirLabel_->setText(tr("Output Directory:"));
    outputDirBrowseBtn_->setText(tr("Browse..."));
    textureSetNameLabel_->setText(tr("Texture Set Name:"));
    vanillaMatchPathLabel_->setText(tr("Vanilla Match Path:"));
    vanillaMatchPathEdit_->setPlaceholderText(tr("e.g. architecture\\whiterun\\wrwoodplank01"));

    // Buttons
    convertButton_->setText(tr("Convert"));
    cancelButton_->setText(tr("Cancel"));
}

void VanillaConversionDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

} // namespace tpbr
