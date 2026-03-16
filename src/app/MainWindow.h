#pragma once

#include "core/Project.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>

namespace tpbr
{

class TexturePreviewWidget;
class MaterialPreviewWidget;
class TextureSetPanel;
class SlotEditorWidget;
class FeatureTogglePanel;
class ParameterPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

  private slots:
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onRenameProject();
    void onExportMod();
    void onBrowseExportFolder();
    void onExportPathEdited();

    void onTextureSetSelected(int index);
    void onAddTextureSet();
    void onRenameTextureSet(int index);
    void onRemoveTextureSet(int index);
    void onImportTexture(PBRTextureSlot slot);
    void onImportChannel(ChannelMap channel);
    void onBatchImport();
    void onDroppedOnSlot(PBRTextureSlot slot, const QString& filePath);
    void onDroppedOnChannel(ChannelMap channel, const QString& filePath);
    void onSlotPreviewRequested(PBRTextureSlot slot);
    void onChannelPreviewRequested(ChannelMap channel);
    void onSlotCleared(PBRTextureSlot slot);
    void onChannelCleared(ChannelMap channel);
    void onRmaosSourceModeChanged(RMAOSSourceMode mode);
    void onMatchTextureChanged(const QString& newPath);
    void onMatchTextureModeChanged(TextureMatchMode mode);
    void onExportCompressionChanged(PBRTextureSlot slot, DDSCompressionMode mode);
    void onExportSizeChanged(PBRTextureSlot slot, int width, int height);
    void onFeaturesChanged(const PBRFeatureFlags& flags);
    void onParametersChanged(const PBRParameters& params);
    void onLandscapeEdidsChanged(const QStringList& edids);
    void onSlotPathOverrideChanged(PBRTextureSlot slot, const QString& path);

  private:
    void setupMenuBar();
    void setupCentralWidget();
    void setupStatusBar();
    void setupLanguageMenu();
    void rebuildLanguageMenu();
    void rebuildRecentProjectsMenu();
    void retranslateUi();
    void changeEvent(QEvent* event) override;
    bool saveProjectToPath(const QString& path);
    QString promptProjectSavePath() const;
    void updateEditorState();
    void refreshUI();
    void refreshPreview();
    void refresh3DPreview();
    void pushAllPreviewSettings();
    void save3DPreviewSettings();
    void restore3DPreviewSettings();

    Project m_project;
    std::filesystem::path m_projectFilePath;
    int m_currentSetIndex = -1;

    // UI components
    TextureSetPanel* m_textureSetPanel = nullptr;
    SlotEditorWidget* m_slotEditor = nullptr;
    FeatureTogglePanel* m_featurePanel = nullptr;
    ParameterPanel* m_paramPanel = nullptr;
    TexturePreviewWidget* m_previewWidget = nullptr;
    MaterialPreviewWidget* m_materialPreview = nullptr;
    ::QLineEdit* m_exportPathEdit = nullptr;
    ::QPushButton* m_exportBrowseBtn = nullptr;
    ::QPushButton* m_exportBtn = nullptr;
    QStackedWidget* m_editorStack = nullptr;
    QStackedWidget* m_previewStack = nullptr;
    QWidget* m_previewModeBar = nullptr;
    QToolButton* m_preview2DBtn = nullptr;
    QToolButton* m_preview3DBtn = nullptr;
    bool m_preview3DMode = false;
    QWidget* m_3dControlBar = nullptr;
    QSlider* m_lightIntensitySlider = nullptr;
    QPushButton* m_lightColorBtn = nullptr;
    QLabel* m_lightIntensityLabel = nullptr;
    QSlider* m_exposureSlider = nullptr;
    QLabel* m_exposureLabel = nullptr;
    QComboBox* m_hdriCombo = nullptr;
    QSlider* m_iblIntensitySlider = nullptr;
    QLabel* m_iblIntensityLabel = nullptr;
    QComboBox* m_iblResCombo = nullptr;
    QComboBox* m_iblSamplesCombo = nullptr;
    QCheckBox* m_horizonOcclusionCB = nullptr;
    QCheckBox* m_multiBounceAOCB = nullptr;
    QCheckBox* m_specularOcclusionCB = nullptr;
    QLabel* m_mipBiasTextLabel = nullptr;
    QSlider* m_mipBiasSlider = nullptr;
    QLabel* m_mipBiasLabel = nullptr;
    QCheckBox* m_vsyncCB = nullptr;
    QCheckBox* m_taaCB = nullptr;
    QCheckBox* m_hdrCB = nullptr;
    QSlider* m_paperWhiteSlider = nullptr;
    QLabel* m_paperWhiteLabel = nullptr;
    QSlider* m_peakBrightnessSlider = nullptr;
    QLabel* m_peakBrightnessLabel = nullptr;
    float m_lightColorR = 1.0f;
    float m_lightColorG = 1.0f;
    float m_lightColorB = 1.0f;
    QComboBox* m_shapeCombo = nullptr;
    QComboBox* m_debugModeCombo = nullptr;
    QLabel* m_editorPlaceholder = nullptr;
    QLabel* m_previewPlaceholder = nullptr;

    // Menu bar items (for retranslation)
    QMenu* m_fileMenu = nullptr;
    QMenu* m_recentMenu = nullptr;
    QMenu* m_languageMenu = nullptr;
    QActionGroup* m_languageGroup = nullptr;
    QAction* m_newProjectAction = nullptr;
    QAction* m_openProjectAction = nullptr;
    QAction* m_saveProjectAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_projectNameAction = nullptr;
    QAction* m_batchImportAction = nullptr;
    QAction* m_exitAction = nullptr;

    // Inline labels that need retranslation
    QLabel* m_exportLabel = nullptr;
    QLabel* m_lightLabel = nullptr;
    QLabel* m_evLabel = nullptr;
    QLabel* m_hdriLabel = nullptr;
    QLabel* m_iblLabel = nullptr;
    QLabel* m_prefilterResLabel = nullptr;
    QLabel* m_samplesLabel = nullptr;
    QLabel* m_paperWhiteTextLabel = nullptr;
    QLabel* m_peakTextLabel = nullptr;
};

} // namespace tpbr
