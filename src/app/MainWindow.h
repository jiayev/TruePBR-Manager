#pragma once

#include "core/Project.h"

#include <QLabel>
#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>

namespace tpbr
{

class TexturePreviewWidget;
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
    void onRmaosSourceModeChanged(RMAOSSourceMode mode);
    void onMatchTextureChanged(const QString& newPath);
    void onMatchTextureModeChanged(TextureMatchMode mode);
    void onExportCompressionChanged(PBRTextureSlot slot, DDSCompressionMode mode);
    void onFeaturesChanged(const PBRFeatureFlags& flags);
    void onParametersChanged(const PBRParameters& params);
    void onLandscapeEdidsChanged(const QStringList& edids);

  private:
    void setupMenuBar();
    void setupCentralWidget();
    void setupStatusBar();
    bool saveProjectToPath(const QString& path);
    QString promptProjectSavePath() const;
    void updateEditorState();
    void refreshUI();
    void refreshPreview();

    Project m_project;
    std::filesystem::path m_projectFilePath;
    int m_currentSetIndex = -1;

    // UI components
    TextureSetPanel* m_textureSetPanel = nullptr;
    SlotEditorWidget* m_slotEditor = nullptr;
    FeatureTogglePanel* m_featurePanel = nullptr;
    ParameterPanel* m_paramPanel = nullptr;
    TexturePreviewWidget* m_previewWidget = nullptr;
    ::QLineEdit* m_exportPathEdit = nullptr;
    ::QPushButton* m_exportBrowseBtn = nullptr;
    ::QPushButton* m_exportBtn = nullptr;
    QStackedWidget* m_editorStack = nullptr;
    QStackedWidget* m_previewStack = nullptr;
    QLabel* m_editorPlaceholder = nullptr;
    QLabel* m_previewPlaceholder = nullptr;
};

} // namespace tpbr
