#pragma once

#include "core/VanillaConverter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>

#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace tpbr
{

/// PBR output texture identifiers for the simultaneous preview grid.
enum class PBROutputSlot
{
    Albedo,
    Normal,
    RMAOS,
    Emissive,
    Displacement,
    Subsurface,
};

/// Single-page dialog for converting vanilla Skyrim textures to True PBR.
///
/// Layout: integrated flow diagram with input thumbnails on the left,
/// arrows pointing to a simultaneous PBR output preview grid on the right.
/// Color textures (Diffuse, Glow, BackLight) get per-texture gamma/brightness controls.
/// All output previews update in real-time as inputs and parameters change.
class VanillaConversionDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit VanillaConversionDialog(QWidget* parent = nullptr);

    /// Build a VanillaConversionInput from the current dialog state.
    VanillaConversionInput getConversionInput() const;

    /// Whether the user accepted the dialog (clicked Convert).
    bool conversionAccepted() const
    {
        return accepted_;
    }

  private slots:
    // -- File selection --
    void onBrowseInput(VanillaTextureType type);
    void onClearInput(VanillaTextureType type);
    void onBrowseOutputDir();

    // -- Preview --
    void onParameterChanged();
    void updateAllPreviews();

    // -- Actions --
    void onConvert();
    void updateConvertButtonState();

  private:
    void setupUi();
    void retranslateUi();
    void changeEvent(QEvent* event) override;

    // -- Input zone (one per vanilla texture type) --
    struct InputZone
    {
        QLabel* thumbnailLabel = nullptr; ///< Small thumbnail preview of input
        QLabel* typeLabel = nullptr;      ///< "Diffuse (Required)" etc.
        QPushButton* browseButton = nullptr;
        QLabel* pathLabel = nullptr; ///< Filename (full path in tooltip)
        QPushButton* clearButton = nullptr;
        QWidget* container = nullptr; ///< Row container widget

        // Per-color-texture gamma/brightness (only for Diffuse, Glow, BackLight)
        QDoubleSpinBox* gammaSpin = nullptr;
        QDoubleSpinBox* brightnessSpin = nullptr;
        QLabel* gammaLabel = nullptr;
        QLabel* brightnessLabel = nullptr;

        std::filesystem::path path;

        // Full-resolution pixels (kept for final conversion, NOT used for preview)
        // Only dimensions are stored here for reference.
        int fullWidth = 0;
        int fullHeight = 0;

        // Downscaled preview pixels (max kPreviewWorkingSize per side).
        // All real-time preview operations run on this small copy.
        std::vector<uint8_t> previewPixels;
        int previewWidth = 0;
        int previewHeight = 0;
    };

    std::map<VanillaTextureType, InputZone> inputZones_;

    // -- Output preview grid (6 PBR output slots shown simultaneously) --
    struct OutputPreview
    {
        QLabel* thumbnailLabel = nullptr; ///< Preview image
        QLabel* nameLabel = nullptr;      ///< "Albedo", "Normal", etc.
        QWidget* container = nullptr;
    };

    std::map<PBROutputSlot, OutputPreview> outputPreviews_;

    // -- Arrow labels (visual flow indicators between input and output) --
    QLabel* flowArrowLabel_ = nullptr;

    // -- Conversion parameters --
    QDoubleSpinBox* shininessSpin_ = nullptr;
    QComboBox* specularModeCombo_ = nullptr;
    QCheckBox* normalAlphaCheck_ = nullptr;
    QCheckBox* metallicRoughnessCheck_ = nullptr;
    QDoubleSpinBox* metallicRoughnessSpin_ = nullptr;

    // -- Output settings --
    QLineEdit* outputDirEdit_ = nullptr;
    QPushButton* outputDirBrowseBtn_ = nullptr;
    QLineEdit* textureSetNameEdit_ = nullptr;
    QLineEdit* vanillaMatchPathEdit_ = nullptr;

    // -- Labels for retranslateUi --
    QLabel* shininessLabel_ = nullptr;
    QLabel* specularModeLabel_ = nullptr;
    QLabel* outputDirLabel_ = nullptr;
    QLabel* textureSetNameLabel_ = nullptr;
    QLabel* vanillaMatchPathLabel_ = nullptr;

    // -- Group boxes --
    QGroupBox* inputGroup_ = nullptr;
    QGroupBox* outputPreviewGroup_ = nullptr;
    QGroupBox* paramGroup_ = nullptr;
    QGroupBox* outputSettingsGroup_ = nullptr;

    // -- Buttons --
    QPushButton* convertButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;

    bool accepted_ = false;

    // -- Preview debounce timer --
    QTimer* previewDebounceTimer_ = nullptr;

    // -- Helpers --

    /// Display name for a vanilla texture type, with "(Required)" suffix
    /// for Diffuse and Normal.
    static QString textureTypeDisplayName(VanillaTextureType type);

    /// Short display name for a PBR output slot.
    static QString outputSlotDisplayName(PBROutputSlot slot);

    /// File-dialog filter string suitable for vanilla texture import.
    static QString imageFileFilter();

    /// Whether a vanilla texture type is a color texture (needs gamma/brightness).
    static bool isColorTexture(VanillaTextureType type);

    /// Load texture pixels into an InputZone's cache. Returns true on success.
    bool loadTexturePixels(VanillaTextureType type);

    /// Generate a QPixmap thumbnail from RGBA pixel data, scaled to fit the given size.
    static QPixmap generateThumbnail(const uint8_t* rgba, int width, int height, int thumbnailSize);

    /// Update the input thumbnail for a specific zone.
    void updateInputThumbnail(VanillaTextureType type);

    /// Generate output preview images based on current inputs and parameters.
    void generateOutputPreviews();

    /// Downscale RGBA pixels to fit within maxSize, preserving aspect ratio.
    /// Returns the downscaled pixels; outW/outH are set to the new dimensions.
    /// If already smaller than maxSize, returns a copy as-is.
    static std::vector<uint8_t> downscaleForPreview(const uint8_t* rgba, int srcW, int srcH, int maxSize, int& outW,
                                                    int& outH);

    /// Preview thumbnail size for input textures.
    static constexpr int kInputThumbnailSize = 64;
    /// Preview thumbnail size for output PBR textures.
    static constexpr int kOutputThumbnailSize = 128;
    /// Maximum working resolution for preview pixel processing.
    /// Textures are downscaled to this size on load; all gamma/brightness/RMAOS
    /// math runs on this small copy instead of the full-resolution source.
    static constexpr int kPreviewWorkingSize = 256;
};

} // namespace tpbr
