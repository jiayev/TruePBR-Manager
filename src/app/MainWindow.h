#pragma once

#include "core/Project.h"

#include <QMainWindow>

namespace tpbr {

class TexturePreviewWidget;
class TextureSetPanel;
class SlotEditorWidget;
class FeatureTogglePanel;
class ParameterPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onExportMod();

    void onTextureSetSelected(int index);
    void onAddTextureSet();
    void onRemoveTextureSet(int index);
    void onImportTexture(PBRTextureSlot slot);
    void onImportChannel(ChannelMap channel);
    void onMatchTextureChanged(const QString& newPath);
    void onFeaturesChanged(const PBRFeatureFlags& flags);
    void onParametersChanged(const PBRParameters& params);

private:
    void setupMenuBar();
    void setupCentralWidget();
    void setupStatusBar();
    void refreshUI();

    Project m_project;
    int     m_currentSetIndex = -1;

    // UI components
    TextureSetPanel*      m_textureSetPanel  = nullptr;
    SlotEditorWidget*     m_slotEditor       = nullptr;
    FeatureTogglePanel*   m_featurePanel     = nullptr;
    ParameterPanel*       m_paramPanel       = nullptr;
    TexturePreviewWidget* m_previewWidget    = nullptr;
};

} // namespace tpbr
