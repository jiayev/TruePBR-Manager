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
#include <QMouseEvent>
#include <QPixmap>
#include <QVBoxLayout>

namespace tpbr
{

// ─── Helpers ───────────────────────────────────────────────

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

QString VanillaConversionDialog::imageFileFilter()
{
    return tr("Image Files (*.png *.dds *.tga *.bmp *.jpg *.jpeg);;All Files (*)");
}

// ─── Constructor ──────────────────────────────────────────

VanillaConversionDialog::VanillaConversionDialog(QWidget* parent) : QDialog(parent)
{
    setupUi();
}

// ─── UI construction ──────────────────────────────────────

void VanillaConversionDialog::setupUi()
{
    setWindowTitle(tr("Convert Vanilla Textures"));
    setMinimumSize(900, 700);

    auto* mainLayout = new QVBoxLayout(this);

    // ── Flow diagram at top ────────────────────────────────
    flowWidget_ = new ConversionFlowWidget(this);
    flowWidget_->setMinimumHeight(200);
    flowWidget_->setMaximumHeight(280);
    mainLayout->addWidget(flowWidget_);

    // ── Middle section: inputs + parameters ─────────────────
    auto* middleLayout = new QHBoxLayout();

    // ── Left panel: Input Files ────────────────────────────
    inputGroup_ = new QGroupBox(tr("Input Files"), this);
    auto* inputLayout = new QGridLayout(inputGroup_);
    inputLayout->setColumnStretch(2, 1); // path label stretches

    static constexpr VanillaTextureType kTextureTypes[] = {
        VanillaTextureType::Diffuse,  VanillaTextureType::Normal,   VanillaTextureType::Glow,
        VanillaTextureType::Parallax, VanillaTextureType::Specular, VanillaTextureType::BackLight,
        VanillaTextureType::EnvMask,  VanillaTextureType::Cubemap,
    };

    int row = 0;
    for (VanillaTextureType type : kTextureTypes)
    {
        InputZone zone;

        zone.typeLabel = new QLabel(textureTypeDisplayName(type), this);
        zone.typeLabel->setMinimumWidth(120);

        zone.browseButton = new QPushButton(tr("Browse..."), this);
        zone.browseButton->setFixedWidth(80);

        zone.pathLabel = new QLabel(tr("Not set"), this);
        zone.pathLabel->setStyleSheet(QStringLiteral("color: gray;"));
        zone.pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        zone.clearButton = new QPushButton(tr("Clear"), this);
        zone.clearButton->setFixedWidth(50);
        zone.clearButton->setEnabled(false);

        inputLayout->addWidget(zone.typeLabel, row, 0);
        inputLayout->addWidget(zone.browseButton, row, 1);
        inputLayout->addWidget(zone.pathLabel, row, 2);
        inputLayout->addWidget(zone.clearButton, row, 3);

        // Connect browse
        connect(zone.browseButton, &QPushButton::clicked, this, [this, type]() { onBrowseInput(type); });

        // Connect clear
        connect(zone.clearButton, &QPushButton::clicked, this, [this, type]() { onClearInput(type); });

        // Make type label and path label clickable for preview
        zone.typeLabel->setCursor(Qt::PointingHandCursor);
        zone.typeLabel->installEventFilter(this);
        zone.pathLabel->setCursor(Qt::PointingHandCursor);
        zone.pathLabel->installEventFilter(this);

        inputZones_[type] = zone;
        ++row;
    }

    middleLayout->addWidget(inputGroup_, 1);

    // ── Right panel: Conversion Parameters ─────────────────
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

    gammaSpin_ = new QDoubleSpinBox(this);
    gammaSpin_->setRange(0.1, 5.0);
    gammaSpin_->setValue(1.0);
    gammaSpin_->setDecimals(2);
    gammaSpin_->setSingleStep(0.01);
    gammaLabel_ = new QLabel(tr("Gamma:"), this);
    paramLayout->addRow(gammaLabel_, gammaSpin_);

    brightnessSpin_ = new QDoubleSpinBox(this);
    brightnessSpin_->setRange(-1.0, 1.0);
    brightnessSpin_->setValue(0.0);
    brightnessSpin_->setDecimals(2);
    brightnessSpin_->setSingleStep(0.01);
    brightnessLabel_ = new QLabel(tr("Brightness:"), this);
    paramLayout->addRow(brightnessLabel_, brightnessSpin_);

    middleLayout->addWidget(paramGroup_);

    mainLayout->addLayout(middleLayout, 1);

    // ── Bottom section: Output + Preview ───────────────────
    auto* bottomLayout = new QHBoxLayout();

    // ── Left: Output settings ──────────────────────────────
    outputGroup_ = new QGroupBox(tr("Output"), this);
    auto* outputLayout = new QFormLayout(outputGroup_);

    auto* dirRowLayout = new QHBoxLayout();
    outputDirEdit_ = new QLineEdit(this);
    outputDirBrowseBtn_ = new QPushButton(tr("Browse..."), this);
    dirRowLayout->addWidget(outputDirEdit_);
    dirRowLayout->addWidget(outputDirBrowseBtn_);
    outputDirLabel_ = new QLabel(tr("Output Directory:"), this);
    outputLayout->addRow(outputDirLabel_, dirRowLayout);

    textureSetNameEdit_ = new QLineEdit(this);
    textureSetNameLabel_ = new QLabel(tr("Texture Set Name:"), this);
    outputLayout->addRow(textureSetNameLabel_, textureSetNameEdit_);

    vanillaMatchPathEdit_ = new QLineEdit(this);
    vanillaMatchPathEdit_->setPlaceholderText(tr("e.g. architecture\\whiterun\\wrwoodplank01"));
    vanillaMatchPathLabel_ = new QLabel(tr("Vanilla Match Path:"), this);
    outputLayout->addRow(vanillaMatchPathLabel_, vanillaMatchPathEdit_);

    bottomLayout->addWidget(outputGroup_, 1);

    // ── Right: Preview ─────────────────────────────────────
    previewGroup_ = new QGroupBox(tr("Preview"), this);
    auto* previewLayout = new QVBoxLayout(previewGroup_);

    previewLabel_ = new QLabel(this);
    previewLabel_->setMinimumSize(300, 300);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setStyleSheet(QStringLiteral("background-color: #2a2a2a; border: 1px solid #555;"));
    previewLabel_->setText(tr("Click a texture to preview"));
    previewLayout->addWidget(previewLabel_, 1);

    previewInfoLabel_ = new QLabel(this);
    previewInfoLabel_->setAlignment(Qt::AlignCenter);
    previewInfoLabel_->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    previewLayout->addWidget(previewInfoLabel_);

    showOriginalCheck_ = new QCheckBox(tr("Show Original"), this);
    previewLayout->addWidget(showOriginalCheck_);

    bottomLayout->addWidget(previewGroup_);

    mainLayout->addLayout(bottomLayout, 1);

    // ── Button row ─────────────────────────────────────────
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    convertButton_ = new QPushButton(tr("Convert"), this);
    convertButton_->setDefault(true);
    convertButton_->setEnabled(false);

    cancelButton_ = new QPushButton(tr("Cancel"), this);

    buttonLayout->addWidget(convertButton_);
    buttonLayout->addWidget(cancelButton_);
    mainLayout->addLayout(buttonLayout);

    // ── Signals ────────────────────────────────────────────

    connect(outputDirBrowseBtn_, &QPushButton::clicked, this, &VanillaConversionDialog::onBrowseOutputDir);
    connect(convertButton_, &QPushButton::clicked, this, &VanillaConversionDialog::onConvert);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);

    // ── Preview debounce timer ─────────────────────────────
    previewDebounceTimer_ = new QTimer(this);
    previewDebounceTimer_->setSingleShot(true);
    previewDebounceTimer_->setInterval(100); // 100ms debounce
    connect(previewDebounceTimer_, &QTimer::timeout, this, &VanillaConversionDialog::updatePreview);

    // ── Gamma/Brightness → debounced preview update ────────
    connect(gammaSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VanillaConversionDialog::onGammaBrightnessChanged);
    connect(brightnessSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VanillaConversionDialog::onGammaBrightnessChanged);

    // ── Show Original toggle ───────────────────────────────
    connect(showOriginalCheck_, &QCheckBox::toggled, this, &VanillaConversionDialog::onShowOriginalToggled);
}

// ─── Slots ─────────────────────────────────────────────────

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

    // Show just the filename in the label, full path in tooltip
    const QString filename = QFileInfo(filePath).fileName();
    zone.pathLabel->setText(filename);
    zone.pathLabel->setToolTip(filePath);
    zone.pathLabel->setStyleSheet(QString());
    zone.clearButton->setEnabled(true);

    flowWidget_->setInputActive(type, true);
    updateConvertButtonState();

    // Load and preview the newly selected texture
    onInputZoneClicked(type);
}

void VanillaConversionDialog::onClearInput(VanillaTextureType type)
{
    auto it = inputZones_.find(type);
    if (it == inputZones_.end())
        return;

    auto& zone = it->second;
    zone.path.clear();
    zone.pathLabel->setText(tr("Not set"));
    zone.pathLabel->setToolTip(QString());
    zone.pathLabel->setStyleSheet(QStringLiteral("color: gray;"));
    zone.clearButton->setEnabled(false);

    flowWidget_->setInputActive(type, false);
    updateConvertButtonState();

    // Clear preview if this texture was being previewed
    if (currentPreview_ && currentPreview_->type == type)
    {
        currentPreview_.reset();
        previewLabel_->setPixmap(QPixmap());
        previewLabel_->setText(tr("Click a texture to preview"));
        previewInfoLabel_->clear();
    }
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

// ─── Preview ───────────────────────────────────────────────

void VanillaConversionDialog::onInputZoneClicked(VanillaTextureType type)
{
    auto it = inputZones_.find(type);
    if (it == inputZones_.end() || it->second.path.empty())
        return;

    loadTextureForPreview(it->second.path, type);
}

bool VanillaConversionDialog::loadTextureForPreview(const std::filesystem::path& path, VanillaTextureType type)
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    int channels = 4;

    // Try DDS first
    const auto ext = path.extension().string();
    bool loaded = false;

    if (ext == ".dds" || ext == ".DDS")
    {
        loaded = DDSUtils::loadDDS(path, width, height, pixels);
    }

    if (!loaded)
    {
        // Try raster formats (PNG, TGA, BMP, JPG)
        auto imageData = ImageUtils::loadImage(path);
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
    {
        previewInfoLabel_->setText(tr("Failed to load texture"));
        return false;
    }

    // Ensure RGBA format (pad RGB to RGBA if needed)
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
        channels = 4;
    }

    // Store original pixels for preview
    currentPreview_ = PreviewState{type, std::move(pixels), width, height, channels};

    // Show info
    previewInfoLabel_->setText(tr("%1 — %2×%3").arg(textureTypeDisplayName(type)).arg(width).arg(height));

    updatePreview();
    return true;
}

void VanillaConversionDialog::onGammaBrightnessChanged()
{
    if (!currentPreview_)
        return;

    // Restart debounce timer (100ms)
    previewDebounceTimer_->start();
}

void VanillaConversionDialog::updatePreview()
{
    if (!currentPreview_)
        return;

    const auto& state = *currentPreview_;

    // Make a working copy of the pixels
    std::vector<uint8_t> pixels = state.originalPixels;

    // Apply gamma/brightness adjustments unless "Show Original" is checked
    if (!showOriginalCheck_->isChecked())
    {
        const float gamma = static_cast<float>(gammaSpin_->value());
        const float brightness = static_cast<float>(brightnessSpin_->value());

        // Only apply if values differ from identity
        if (gamma != 1.0f || brightness != 0.0f)
        {
            VanillaConverter::applyGammaBrightness(pixels.data(), state.width, state.height, gamma, brightness);
        }
    }

    // Convert to QImage (RGBA8 → QImage::Format_RGBA8888)
    // QImage requires the data to remain valid for the lifetime of the image,
    // so we construct it and immediately convert to QPixmap.
    QImage image(pixels.data(), state.width, state.height, state.width * 4, QImage::Format_RGBA8888);

    // Scale to fit preview label while maintaining aspect ratio
    const QSize targetSize = previewLabel_->size() - QSize(4, 4); // Account for border
    QPixmap pixmap = QPixmap::fromImage(image).scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    previewLabel_->setPixmap(pixmap);
}

void VanillaConversionDialog::onShowOriginalToggled(bool /*showOriginal*/)
{
    updatePreview();
}

// ─── Result extraction ─────────────────────────────────────

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
    input.params.gamma = static_cast<float>(gammaSpin_->value());
    input.params.brightness = static_cast<float>(brightnessSpin_->value());

    const int specIdx = specularModeCombo_->currentIndex();
    if (specIdx >= 0)
        input.params.specularMode = static_cast<SpecularMode>(specularModeCombo_->itemData(specIdx).toInt());

    input.params.normalAlphaIsSpecular = normalAlphaCheck_->isChecked();

    // Output
    input.outputDir = outputDirEdit_->text().toStdString();
    input.textureSetName = textureSetNameEdit_->text().toStdString();
    input.vanillaMatchPath = vanillaMatchPathEdit_->text().toStdString();

    return input;
}

// ─── Event filter (click label → preview) ──────────────────

bool VanillaConversionDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress)
    {
        // Find which input zone's label was clicked
        for (auto& [type, zone] : inputZones_)
        {
            if ((watched == zone.typeLabel || watched == zone.pathLabel) && !zone.path.empty())
            {
                onInputZoneClicked(type);
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

// ─── i18n ──────────────────────────────────────────────────

void VanillaConversionDialog::retranslateUi()
{
    setWindowTitle(tr("Convert Vanilla Textures"));

    inputGroup_->setTitle(tr("Input Files"));
    paramGroup_->setTitle(tr("Conversion Parameters"));
    outputGroup_->setTitle(tr("Output"));

    // Input zone labels
    for (auto& [type, zone] : inputZones_)
    {
        zone.typeLabel->setText(textureTypeDisplayName(type));
        zone.browseButton->setText(tr("Browse..."));
        zone.clearButton->setText(tr("Clear"));
        if (zone.path.empty())
            zone.pathLabel->setText(tr("Not set"));
    }

    // Parameter labels
    shininessLabel_->setText(tr("Shininess:"));
    specularModeLabel_->setText(tr("Specular Mode:"));
    normalAlphaCheck_->setText(tr("Normal Alpha is Specular"));
    gammaLabel_->setText(tr("Gamma:"));
    brightnessLabel_->setText(tr("Brightness:"));

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

    // Preview
    previewGroup_->setTitle(tr("Preview"));
    showOriginalCheck_->setText(tr("Show Original"));
    if (!currentPreview_)
        previewLabel_->setText(tr("Click a texture to preview"));
}

void VanillaConversionDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

} // namespace tpbr
