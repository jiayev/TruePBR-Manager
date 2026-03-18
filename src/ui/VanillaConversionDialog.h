#pragma once

#include "core/VanillaConverter.h"
#include "ui/ConversionFlowWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>

#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace tpbr
{

/// Single-page dialog for converting vanilla Skyrim textures to True PBR.
///
/// Presents a flow diagram, 8 file import zones (Diffuse/Normal required),
/// conversion parameters, and output settings.  The Convert button is
/// enabled only when both Diffuse and Normal textures are selected.
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
    // ── File selection ──
    void onBrowseInput(VanillaTextureType type);
    void onClearInput(VanillaTextureType type);
    void onBrowseOutputDir();

    // ── Preview ──
    void onInputZoneClicked(VanillaTextureType type);
    void onGammaBrightnessChanged();
    void updatePreview();
    void onShowOriginalToggled(bool showOriginal);

    // ── Actions ──
    void onConvert();
    void updateConvertButtonState();

  private:
    void setupUi();
    void retranslateUi();
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    // ── Flow diagram ──────────────────────────────────────
    ConversionFlowWidget* flowWidget_ = nullptr;

    // ── Input file zones ──────────────────────────────────
    struct InputZone
    {
        QLabel* typeLabel = nullptr;
        QPushButton* browseButton = nullptr;
        QLabel* pathLabel = nullptr;
        QPushButton* clearButton = nullptr;
        std::filesystem::path path;
    };

    std::map<VanillaTextureType, InputZone> inputZones_;

    // ── Conversion parameters ─────────────────────────────
    QDoubleSpinBox* shininessSpin_ = nullptr;
    QComboBox* specularModeCombo_ = nullptr;
    QCheckBox* normalAlphaCheck_ = nullptr;
    QDoubleSpinBox* gammaSpin_ = nullptr;
    QDoubleSpinBox* brightnessSpin_ = nullptr;

    // ── Output settings ───────────────────────────────────
    QLineEdit* outputDirEdit_ = nullptr;
    QPushButton* outputDirBrowseBtn_ = nullptr;
    QLineEdit* textureSetNameEdit_ = nullptr;
    QLineEdit* vanillaMatchPathEdit_ = nullptr;

    // ── Labels for retranslateUi ──────────────────────────
    QLabel* shininessLabel_ = nullptr;
    QLabel* specularModeLabel_ = nullptr;
    QLabel* gammaLabel_ = nullptr;
    QLabel* brightnessLabel_ = nullptr;
    QLabel* outputDirLabel_ = nullptr;
    QLabel* textureSetNameLabel_ = nullptr;
    QLabel* vanillaMatchPathLabel_ = nullptr;

    // ── Group boxes ───────────────────────────────────────
    QGroupBox* inputGroup_ = nullptr;
    QGroupBox* paramGroup_ = nullptr;
    QGroupBox* outputGroup_ = nullptr;
    QGroupBox* previewGroup_ = nullptr;

    // ── Buttons ───────────────────────────────────────────
    QPushButton* convertButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;

    bool accepted_ = false;

    // ── Preview ───────────────────────────────────────────
    QLabel* previewLabel_ = nullptr;
    QCheckBox* showOriginalCheck_ = nullptr;
    QTimer* previewDebounceTimer_ = nullptr;
    QLabel* previewInfoLabel_ = nullptr;

    struct PreviewState
    {
        VanillaTextureType type;
        std::vector<uint8_t> originalPixels;
        int width = 0;
        int height = 0;
        int channels = 0;
    };
    std::optional<PreviewState> currentPreview_;

    // ── Helpers ───────────────────────────────────────────

    /// Display name for a vanilla texture type, with "(Required)" suffix
    /// for Diffuse and Normal.
    static QString textureTypeDisplayName(VanillaTextureType type);

    /// File-dialog filter string suitable for vanilla texture import.
    static QString imageFileFilter();

    /// Load texture pixels for preview (DDS or raster format).
    /// Returns false if loading fails.
    bool loadTextureForPreview(const std::filesystem::path& path, VanillaTextureType type);
};

} // namespace tpbr
