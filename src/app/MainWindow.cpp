#include "MainWindow.h"

#include "core/AppSettings.h"
#include "core/JsonExporter.h"
#include "core/ModExporter.h"
#include "core/TextureImporter.h"
#include "core/TextureSetValidator.h"
#include "core/TranslationManager.h"
#include "ui/FeatureTogglePanel.h"
#include "ui/ParameterPanel.h"
#include "ui/SlotEditorWidget.h"
#include "ui/TexturePreviewWidget.h"
#include "ui/TextureSetPanel.h"
#include "ui/MaterialPreviewWidget.h"

#include "renderer/IBLPipeline.h"

#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"
#include "utils/ImageUtils.h"
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QThread>
#include <QVBoxLayout>

#include "utils/Log.h"

namespace tpbr
{

static const char* channelDisplayName(ChannelMap channel)
{
    switch (channel)
    {
    case ChannelMap::Roughness:
        return "Roughness";
    case ChannelMap::Metallic:
        return "Metallic";
    case ChannelMap::AO:
        return "AO";
    case ChannelMap::Specular:
        return "Specular";
    default:
        return "Unknown";
    }
}

static QImage loadPreviewImage(const std::filesystem::path& path)
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
        return image.copy();
    }

    return QImage(QString::fromStdString(path.string()));
}

static QString defaultProjectName(const Project& project)
{
    const QString projectName = QString::fromStdString(project.name).trimmed();
    return projectName.isEmpty() ? QStringLiteral("Untitled") : projectName;
}

static bool isPlaceholderProjectName(const Project& project)
{
    return defaultProjectName(project).compare(QStringLiteral("Untitled"), Qt::CaseInsensitive) == 0;
}

static QLabel* createPlaceholderLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMargin(24);
    label->setStyleSheet("QLabel { color: #b0b0b0; font-size: 14px; }");
    return label;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(tr("TruePBR Manager"));
    resize(1280, 800);

    // Restore saved window geometry
    auto geom = AppSettings::instance().windowGeometry();
    if (!geom.isEmpty())
        restoreGeometry(geom);
    auto state = AppSettings::instance().windowState();
    if (!state.isEmpty())
        restoreState(state);

    m_project.name = "Untitled";

    setupMenuBar();
    setupCentralWidget();
    setupStatusBar();
    restore3DPreviewSettings();
    refreshUI();
}

MainWindow::~MainWindow()
{
    AppSettings::instance().setWindowGeometry(saveGeometry());
    AppSettings::instance().setWindowState(saveState());
    save3DPreviewSettings();
}

// ─── Menu Bar ──────────────────────────────────────────────

void MainWindow::setupMenuBar()
{
    m_fileMenu = menuBar()->addMenu(tr("&File"));
    m_newProjectAction = m_fileMenu->addAction(tr("&New Project"), this, &MainWindow::onNewProject, QKeySequence::New);
    m_openProjectAction =
        m_fileMenu->addAction(tr("&Open Project"), this, &MainWindow::onOpenProject, QKeySequence::Open);
    m_recentMenu = m_fileMenu->addMenu(tr("Recent &Projects"));
    rebuildRecentProjectsMenu();
    m_saveProjectAction =
        m_fileMenu->addAction(tr("&Save Project"), this, &MainWindow::onSaveProject, QKeySequence::Save);
    m_saveAsAction =
        m_fileMenu->addAction(tr("Save Project &As..."), this, &MainWindow::onSaveProjectAs, QKeySequence::SaveAs);
    m_projectNameAction = m_fileMenu->addAction(tr("Project &Name..."), this, &MainWindow::onRenameProject);
    m_fileMenu->addSeparator();
    m_batchImportAction = m_fileMenu->addAction(tr("&Batch Import Folder..."), this, &MainWindow::onBatchImport);
    m_fileMenu->addSeparator();
    m_exitAction = m_fileMenu->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence::Quit);

    setupLanguageMenu();
}

void MainWindow::rebuildRecentProjectsMenu()
{
    m_recentMenu->clear();
    const QStringList recent = AppSettings::instance().recentProjects();

    if (recent.isEmpty())
    {
        auto* emptyAction = m_recentMenu->addAction(tr("(No recent projects)"));
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString& path : recent)
    {
        const QString label = QFileInfo(path).fileName();
        auto* action = m_recentMenu->addAction(label);
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this,
                [this, path]()
                {
                    if (!QFileInfo::exists(path))
                    {
                        QMessageBox::warning(
                            this, tr("Recent Project"),
                            tr("File not found:\n%1\n\nIt will be removed from the recent list.").arg(path));
                        AppSettings::instance().removeRecentProject(path);
                        rebuildRecentProjectsMenu();
                        return;
                    }
                    AppSettings::instance().setLastProjectDir(QFileInfo(path).absolutePath());
                    m_project = Project::load(path.toStdString());
                    m_projectFilePath = path.toStdString();
                    m_currentSetIndex = -1;
                    AppSettings::instance().addRecentProject(path);
                    rebuildRecentProjectsMenu();
                    refreshUI();
                    statusBar()->showMessage(tr("Opened: %1").arg(path));
                });
    }

    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("Clear Recent Projects"), this,
                            [this]()
                            {
                                AppSettings::instance().setValue("RecentProjects/paths", QStringList());
                                rebuildRecentProjectsMenu();
                            });
}

// ─── Central Widget ────────────────────────────────────────

void MainWindow::setupCentralWidget()
{
    auto* rootWidget = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(rootWidget);

    auto* exportRow = new QHBoxLayout();
    m_exportLabel = new QLabel(tr("Export Folder:"), rootWidget);
    m_exportLabel->setFixedWidth(100);
    m_exportPathEdit = new QLineEdit(rootWidget);
    m_exportPathEdit->setPlaceholderText(tr("Select the target mod folder"));
    m_exportBrowseBtn = new QPushButton(tr("Browse..."), rootWidget);
    m_exportBtn = new QPushButton(tr("Export Mod"), rootWidget);
    exportRow->addWidget(m_exportLabel);
    exportRow->addWidget(m_exportPathEdit, 1);
    exportRow->addWidget(m_exportBrowseBtn);
    exportRow->addWidget(m_exportBtn);
    rootLayout->addLayout(exportRow);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left: Texture Set List
    m_textureSetPanel = new TextureSetPanel(this);
    splitter->addWidget(m_textureSetPanel);

    // Middle: Slot Editor + Feature Toggles + Parameters (scrollable)
    auto* middleScroll = new QScrollArea(this);
    middleScroll->setWidgetResizable(true);
    middleScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* middleWidget = new QWidget(this);
    auto* middleLayout = new QVBoxLayout(middleWidget);

    m_slotEditor = new SlotEditorWidget(this);
    m_featurePanel = new FeatureTogglePanel(this);
    m_paramPanel = new ParameterPanel(this);

    middleLayout->addWidget(m_slotEditor);
    middleLayout->addWidget(m_featurePanel);
    middleLayout->addWidget(m_paramPanel);
    middleLayout->addStretch();

    middleScroll->setWidget(middleWidget);

    m_editorStack = new QStackedWidget(this);
    m_editorStack->addWidget(middleScroll);
    m_editorPlaceholder = createPlaceholderLabel(tr("No Texture Set selected.\n\nUse Add to create one, or select an "
                                                    "existing Texture Set from the list on the left."),
                                                 m_editorStack);
    m_editorStack->addWidget(m_editorPlaceholder);
    splitter->addWidget(m_editorStack);

    // Right: Preview area with 2D/3D toggle
    auto* previewContainer = new QWidget(this);
    auto* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(2);

    // 2D/3D toggle bar
    m_previewModeBar = new QWidget(previewContainer);
    auto* modeBarLayout = new QHBoxLayout(m_previewModeBar);
    modeBarLayout->setContentsMargins(4, 2, 4, 2);
    modeBarLayout->setSpacing(2);

    m_preview2DBtn = new QToolButton(m_previewModeBar);
    m_preview2DBtn->setText(tr("2D"));
    m_preview2DBtn->setCheckable(true);
    m_preview2DBtn->setChecked(true);
    m_preview2DBtn->setFixedSize(48, 24);

    m_preview3DBtn = new QToolButton(m_previewModeBar);
    m_preview3DBtn->setText(tr("3D"));
    m_preview3DBtn->setCheckable(true);
    m_preview3DBtn->setFixedSize(48, 24);

    modeBarLayout->addWidget(m_preview2DBtn);
    modeBarLayout->addWidget(m_preview3DBtn);
    modeBarLayout->addStretch();

    previewLayout->addWidget(m_previewModeBar);

    // Preview stack: 2D texture viewer, 3D material viewer, placeholder
    m_previewWidget = new TexturePreviewWidget(this);
    m_materialPreview = new MaterialPreviewWidget(this);

    m_previewStack = new QStackedWidget(this);
    m_previewStack->addWidget(m_previewWidget);   // index 0 = 2D
    m_previewStack->addWidget(m_materialPreview); // index 1 = 3D
    m_previewPlaceholder = createPlaceholderLabel(
        tr("Preview is unavailable until a Texture Set is selected and at least one texture is imported."),
        m_previewStack);
    m_previewStack->addWidget(m_previewPlaceholder); // index 2 = placeholder

    previewLayout->addWidget(m_previewStack, 1);

    // Shape combo from MaterialPreviewWidget (show below preview in 3D mode)
    m_shapeCombo = m_materialPreview->shapeCombo();
    m_shapeCombo->setParent(previewContainer);
    m_shapeCombo->setVisible(false);

    // Debug visualization mode combo
    m_debugModeCombo = new QComboBox(previewContainer);
    m_debugModeCombo->addItem(tr("Full Shading"), 0);
    m_debugModeCombo->addItem(tr("Normal"), 1);
    m_debugModeCombo->addItem(tr("Roughness"), 2);
    m_debugModeCombo->addItem(tr("Metallic"), 3);
    m_debugModeCombo->addItem(tr("AO"), 4);
    m_debugModeCombo->addItem(tr("Specular"), 5);
    m_debugModeCombo->setVisible(false);

    auto* shapeDebugRow = new QHBoxLayout();
    shapeDebugRow->setContentsMargins(0, 0, 0, 0);
    shapeDebugRow->addWidget(m_shapeCombo, 1);
    shapeDebugRow->addWidget(m_debugModeCombo, 1);
    previewLayout->addLayout(shapeDebugRow);

    connect(m_debugModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                uint32_t mode = static_cast<uint32_t>(m_debugModeCombo->itemData(index).toInt());
                m_materialPreview->setDebugMode(mode);
            });

    // 3D control bar: light intensity slider + light color button
    m_3dControlBar = new QWidget(previewContainer);
    auto* controlLayout = new QHBoxLayout(m_3dControlBar);
    controlLayout->setContentsMargins(4, 2, 4, 2);
    controlLayout->setSpacing(6);

    m_lightLabel = new QLabel(tr("Light:"), m_3dControlBar);
    controlLayout->addWidget(m_lightLabel);

    m_lightIntensitySlider = new QSlider(Qt::Horizontal, m_3dControlBar);
    m_lightIntensitySlider->setRange(0, 100);
    m_lightIntensitySlider->setValue(30); // default 3.0
    m_lightIntensitySlider->setToolTip(tr("Light Intensity"));
    controlLayout->addWidget(m_lightIntensitySlider, 1);

    m_lightIntensityLabel = new QLabel("3.0", m_3dControlBar);
    m_lightIntensityLabel->setFixedWidth(32);
    controlLayout->addWidget(m_lightIntensityLabel);

    m_lightColorBtn = new QPushButton(m_3dControlBar);
    m_lightColorBtn->setFixedSize(28, 28);
    m_lightColorBtn->setToolTip(tr("Light Color"));
    m_lightColorBtn->setStyleSheet("QPushButton { background: #ffffff; border: 1px solid #888; }");
    controlLayout->addWidget(m_lightColorBtn);

    m_evLabel = new QLabel(tr("EV:"), m_3dControlBar);
    controlLayout->addWidget(m_evLabel);

    m_exposureSlider = new QSlider(Qt::Horizontal, m_3dControlBar);
    m_exposureSlider->setRange(-30, 30);
    m_exposureSlider->setValue(0); // default 0.0 EV
    m_exposureSlider->setToolTip(tr("Exposure Compensation (EV)"));
    controlLayout->addWidget(m_exposureSlider, 1);

    m_exposureLabel = new QLabel("0.0", m_3dControlBar);
    m_exposureLabel->setFixedWidth(32);
    controlLayout->addWidget(m_exposureLabel);

    m_3dControlBar->setVisible(false);
    previewLayout->addWidget(m_3dControlBar);

    // Render options row
    auto* renderOptsRow = new QWidget(previewContainer);
    auto* renderOptsLayout = new QHBoxLayout(renderOptsRow);
    renderOptsLayout->setContentsMargins(4, 2, 4, 2);
    renderOptsLayout->setSpacing(8);

    m_horizonOcclusionCB = new QCheckBox(tr("Horizon Occlusion"), renderOptsRow);
    m_horizonOcclusionCB->setChecked(true);
    m_horizonOcclusionCB->setToolTip(tr("Attenuate specular IBL below geometric horizon"));
    renderOptsLayout->addWidget(m_horizonOcclusionCB);

    m_multiBounceAOCB = new QCheckBox(tr("MultiBounce AO"), renderOptsRow);
    m_multiBounceAOCB->setChecked(true);
    m_multiBounceAOCB->setToolTip(tr("Multi-bounce ambient occlusion (Jimenez 2016)"));
    renderOptsLayout->addWidget(m_multiBounceAOCB);

    m_specularOcclusionCB = new QCheckBox(tr("Specular Occlusion"), renderOptsRow);
    m_specularOcclusionCB->setChecked(true);
    m_specularOcclusionCB->setToolTip(tr("AO-based specular occlusion"));
    renderOptsLayout->addWidget(m_specularOcclusionCB);

    renderOptsLayout->addStretch();
    renderOptsRow->setVisible(false);
    previewLayout->addWidget(renderOptsRow);

    // Display options row (VSync, HDR, Paper White, Peak Brightness)
    auto* displayOptsRow = new QWidget(previewContainer);
    auto* displayOptsLayout = new QHBoxLayout(displayOptsRow);
    displayOptsLayout->setContentsMargins(4, 2, 4, 2);
    displayOptsLayout->setSpacing(8);

    m_vsyncCB = new QCheckBox(tr("VSync"), displayOptsRow);
    m_vsyncCB->setChecked(true);
    m_vsyncCB->setToolTip(tr("Vertical sync (off = uncapped frame rate with tearing support)"));
    displayOptsLayout->addWidget(m_vsyncCB);

    m_taaCB = new QCheckBox(tr("TAA"), displayOptsRow);
    m_taaCB->setChecked(true);
    m_taaCB->setToolTip(tr("Temporal Anti-Aliasing"));
    displayOptsLayout->addWidget(m_taaCB);

    m_hdrCB = new QCheckBox(tr("HDR"), displayOptsRow);
    m_hdrCB->setChecked(false);
    m_hdrCB->setToolTip(tr("Enable HDR output (scRGB) — requires Windows HDR enabled"));
    displayOptsLayout->addWidget(m_hdrCB);

    m_paperWhiteTextLabel = new QLabel(tr("Paper White:"), displayOptsRow);
    displayOptsLayout->addWidget(m_paperWhiteTextLabel);
    m_paperWhiteSlider = new QSlider(Qt::Horizontal, displayOptsRow);
    m_paperWhiteSlider->setRange(80, 400);
    m_paperWhiteSlider->setValue(200);
    m_paperWhiteSlider->setToolTip(tr("Paper white brightness in nits (SDR reference white)"));
    m_paperWhiteSlider->setEnabled(false);
    displayOptsLayout->addWidget(m_paperWhiteSlider, 1);

    m_paperWhiteLabel = new QLabel("200", displayOptsRow);
    m_paperWhiteLabel->setFixedWidth(32);
    displayOptsLayout->addWidget(m_paperWhiteLabel);

    m_peakTextLabel = new QLabel(tr("Peak:"), displayOptsRow);
    displayOptsLayout->addWidget(m_peakTextLabel);
    m_peakBrightnessSlider = new QSlider(Qt::Horizontal, displayOptsRow);
    m_peakBrightnessSlider->setRange(200, 2000);
    m_peakBrightnessSlider->setValue(1000);
    m_peakBrightnessSlider->setToolTip(tr("Peak brightness in nits (0 = use display maximum)"));
    m_peakBrightnessSlider->setEnabled(false);
    displayOptsLayout->addWidget(m_peakBrightnessSlider, 1);

    m_peakBrightnessLabel = new QLabel("1000", displayOptsRow);
    m_peakBrightnessLabel->setFixedWidth(40);
    displayOptsLayout->addWidget(m_peakBrightnessLabel);

    displayOptsRow->setVisible(false);
    previewLayout->addWidget(displayOptsRow);

    auto updateDisplayOpts = [this]()
    {
        m_materialPreview->setVSync(m_vsyncCB->isChecked());
        m_materialPreview->setTAAEnabled(m_taaCB->isChecked());
    };
    connect(m_vsyncCB, &QCheckBox::toggled, this, updateDisplayOpts);
    connect(m_taaCB, &QCheckBox::toggled, this, updateDisplayOpts);

    connect(m_hdrCB, &QCheckBox::toggled, this,
            [this](bool checked)
            {
                m_materialPreview->setHDREnabled(checked);
                bool hdrActive = m_materialPreview->isHDREnabled();
                if (checked && !hdrActive)
                {
                    m_hdrCB->blockSignals(true);
                    m_hdrCB->setChecked(false);
                    m_hdrCB->blockSignals(false);
                    statusBar()->showMessage(tr("HDR not available — enable HDR in Windows Display Settings"), 5000);
                }
                m_paperWhiteSlider->setEnabled(hdrActive);
                m_peakBrightnessSlider->setEnabled(hdrActive);
            });

    connect(m_paperWhiteSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                m_paperWhiteLabel->setText(QString::number(value));
                m_materialPreview->setPaperWhiteNits(static_cast<float>(value));
            });

    connect(m_peakBrightnessSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                m_peakBrightnessLabel->setText(QString::number(value));
                m_materialPreview->setPeakBrightnessNits(static_cast<float>(value));
            });

    auto updateRenderFlags = [this]()
    {
        uint32_t flags = 0;
        if (m_horizonOcclusionCB->isChecked())
            flags |= 1u;
        if (m_multiBounceAOCB->isChecked())
            flags |= 2u;
        if (m_specularOcclusionCB->isChecked())
            flags |= 4u;
        m_materialPreview->setRenderFlags(flags);
    };

    connect(m_horizonOcclusionCB, &QCheckBox::toggled, this, updateRenderFlags);
    connect(m_multiBounceAOCB, &QCheckBox::toggled, this, updateRenderFlags);
    connect(m_specularOcclusionCB, &QCheckBox::toggled, this, updateRenderFlags);

    connect(m_lightIntensitySlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                float intensity = static_cast<float>(value) / 10.0f;
                m_lightIntensityLabel->setText(QString::number(intensity, 'f', 1));
                m_materialPreview->setLightIntensity(intensity);
            });

    connect(m_exposureSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                float ev = static_cast<float>(value) / 10.0f;
                m_exposureLabel->setText(QString::number(ev, 'f', 1));
                m_materialPreview->setExposure(ev);
            });

    connect(m_lightColorBtn, &QPushButton::clicked, this,
            [this]()
            {
                QColor initial = QColor::fromRgbF(m_lightColorR, m_lightColorG, m_lightColorB);
                QColor color = QColorDialog::getColor(initial, this, tr("Light Color"));
                if (color.isValid())
                {
                    m_lightColorR = static_cast<float>(color.redF());
                    m_lightColorG = static_cast<float>(color.greenF());
                    m_lightColorB = static_cast<float>(color.blueF());
                    m_lightColorBtn->setStyleSheet(
                        QString("QPushButton { background: %1; border: 1px solid #888; }").arg(color.name()));
                    m_materialPreview->setLightColor(m_lightColorR, m_lightColorG, m_lightColorB);
                }
            });

    // IBL row
    auto* iblRow = new QWidget(previewContainer);
    auto* iblLayout = new QHBoxLayout(iblRow);
    iblLayout->setContentsMargins(4, 2, 4, 2);
    iblLayout->setSpacing(6);

    m_hdriLabel = new QLabel(tr("HDRI:"), iblRow);
    iblLayout->addWidget(m_hdriLabel);

    m_hdriCombo = new QComboBox(iblRow);
    m_hdriCombo->addItem(tr("(None)"), QString());
    m_hdriCombo->setMinimumWidth(150);
    iblLayout->addWidget(m_hdriCombo);

    m_iblLabel = new QLabel(tr("IBL:"), iblRow);
    iblLayout->addWidget(m_iblLabel);

    m_iblIntensitySlider = new QSlider(Qt::Horizontal, iblRow);
    m_iblIntensitySlider->setRange(0, 50);
    m_iblIntensitySlider->setValue(10); // default 1.0
    m_iblIntensitySlider->setToolTip(tr("IBL Intensity"));
    iblLayout->addWidget(m_iblIntensitySlider, 1);

    m_iblIntensityLabel = new QLabel("1.0", iblRow);
    m_iblIntensityLabel->setFixedWidth(32);
    iblLayout->addWidget(m_iblIntensityLabel);

    iblRow->setVisible(false);
    previewLayout->addWidget(iblRow);

    // IBL specular parameter row
    auto* iblParamsRow = new QWidget(previewContainer);
    auto* iblParamsLayout = new QHBoxLayout(iblParamsRow);
    iblParamsLayout->setContentsMargins(4, 2, 4, 2);
    iblParamsLayout->setSpacing(6);

    m_prefilterResLabel = new QLabel(tr("Prefilter Res:"), iblParamsRow);
    iblParamsLayout->addWidget(m_prefilterResLabel);
    m_iblResCombo = new QComboBox(iblParamsRow);
    m_iblResCombo->addItem("32", 32);
    m_iblResCombo->addItem("64", 64);
    m_iblResCombo->addItem("128", 128);
    m_iblResCombo->addItem("256", 256);
    m_iblResCombo->addItem("512", 512);
    m_iblResCombo->addItem("1024", 1024);
    m_iblResCombo->setCurrentIndex(3); // default 256
    m_iblResCombo->setToolTip(tr("Prefiltered cubemap face resolution"));
    iblParamsLayout->addWidget(m_iblResCombo);

    m_samplesLabel = new QLabel(tr("Samples:"), iblParamsRow);
    iblParamsLayout->addWidget(m_samplesLabel);
    m_iblSamplesCombo = new QComboBox(iblParamsRow);
    m_iblSamplesCombo->addItem("64", 64);
    m_iblSamplesCombo->addItem("128", 128);
    m_iblSamplesCombo->addItem("256", 256);
    m_iblSamplesCombo->addItem("512", 512);
    m_iblSamplesCombo->addItem("1024", 1024);
    m_iblSamplesCombo->setCurrentIndex(2); // default 256
    m_iblSamplesCombo->setToolTip(tr("GGX importance samples per texel for specular prefiltering"));
    iblParamsLayout->addWidget(m_iblSamplesCombo);

    iblParamsLayout->addStretch();

    iblParamsRow->setVisible(false);
    previewLayout->addWidget(iblParamsRow);

    auto applyIBLParams = [this]()
    {
        int res = m_iblResCombo->currentData().toInt();
        int samples = m_iblSamplesCombo->currentData().toInt();
        statusBar()->showMessage(tr("Reprocessing IBL (res=%1, samples=%2)...").arg(res).arg(samples));
        QApplication::processEvents();
        m_materialPreview->setIBLParams(res, samples);
        statusBar()->showMessage(tr("IBL reprocessed (res=%1, samples=%2)").arg(res).arg(samples), 3000);
    };

    connect(m_iblResCombo, &QComboBox::currentIndexChanged, this, applyIBLParams);
    connect(m_iblSamplesCombo, &QComboBox::currentIndexChanged, this, applyIBLParams);

    // Populate HDRI combo from hdris/ directory
    {
        // Look next to exe first (dist), then source tree (dev)
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        auto exeDir = std::filesystem::path(exePath).parent_path();
        auto distHdriDir = exeDir / "hdris";
        auto srcHdriDir =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "resources" / "HDRIs";

        auto scanDir = [this](const std::filesystem::path& dir)
        {
            auto files = IBLPipeline::listHDRIs(dir);
            for (const auto& f : files)
            {
                m_hdriCombo->addItem(QString::fromStdString(f.stem().string()), QString::fromStdString(f.string()));
            }
        };

        scanDir(distHdriDir);
        if (std::filesystem::exists(srcHdriDir) && srcHdriDir != distHdriDir)
        {
            scanDir(srcHdriDir);
        }
    }

    connect(m_hdriCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                QString path = m_hdriCombo->itemData(index).toString();
                if (path.isEmpty())
                {
                    m_materialPreview->unloadIBL();
                    statusBar()->showMessage(tr("IBL disabled"));
                }
                else
                {
                    statusBar()->showMessage(tr("Loading HDRI..."));
                    QApplication::processEvents();
                    if (m_materialPreview->loadIBL(path.toStdString()))
                    {
                        float intensity = static_cast<float>(m_iblIntensitySlider->value()) / 10.0f;
                        m_materialPreview->setIBLIntensity(intensity);
                        statusBar()->showMessage(tr("HDRI loaded: %1").arg(m_hdriCombo->currentText()));
                    }
                    else
                    {
                        statusBar()->showMessage(tr("Failed to load HDRI"));
                    }
                }
            });

    connect(m_iblIntensitySlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                float intensity = static_cast<float>(value) / 10.0f;
                m_iblIntensityLabel->setText(QString::number(intensity, 'f', 1));
                m_materialPreview->setIBLIntensity(intensity);
            });

    splitter->addWidget(previewContainer);

    // 2D/3D toggle connections
    connect(m_preview2DBtn, &QToolButton::clicked, this,
            [this, iblRow, iblParamsRow, renderOptsRow, displayOptsRow]()
            {
                m_preview3DMode = false;
                m_preview2DBtn->setChecked(true);
                m_preview3DBtn->setChecked(false);
                m_shapeCombo->setVisible(false);
                m_debugModeCombo->setVisible(false);
                m_3dControlBar->setVisible(false);
                iblRow->setVisible(false);
                iblParamsRow->setVisible(false);
                renderOptsRow->setVisible(false);
                displayOptsRow->setVisible(false);
                refreshPreview();
            });
    connect(m_preview3DBtn, &QToolButton::clicked, this,
            [this, iblRow, iblParamsRow, renderOptsRow, displayOptsRow]()
            {
                m_preview3DMode = true;
                m_preview2DBtn->setChecked(false);
                m_preview3DBtn->setChecked(true);
                m_shapeCombo->setVisible(true);
                m_debugModeCombo->setVisible(true);
                m_3dControlBar->setVisible(true);
                iblRow->setVisible(true);
                iblParamsRow->setVisible(true);
                renderOptsRow->setVisible(true);
                displayOptsRow->setVisible(true);

                refresh3DPreview();

                // Update HDR checkbox availability (after refresh, which ensures renderer is initialized)
                auto hdrInfo = m_materialPreview->queryHDRSupport();
                m_hdrCB->setEnabled(hdrInfo.hdrSupported);
                if (!hdrInfo.hdrSupported)
                    m_hdrCB->setToolTip(tr("HDR not available — enable HDR in Windows Display Settings"));
                else
                {
                    m_hdrCB->setToolTip(
                        tr("Enable HDR output (scRGB, peak %.0f nits)").arg(static_cast<double>(hdrInfo.maxLuminance)));
                    m_peakBrightnessSlider->setMaximum(static_cast<int>(hdrInfo.maxLuminance));
                }
            });

    splitter->setStretchFactor(0, 1); // list
    splitter->setStretchFactor(1, 2); // editor
    splitter->setStretchFactor(2, 3); // preview

    rootLayout->addWidget(splitter, 1);
    setCentralWidget(rootWidget);

    // Connections
    connect(m_exportPathEdit, &QLineEdit::editingFinished, this, &MainWindow::onExportPathEdited);
    connect(m_exportBrowseBtn, &QPushButton::clicked, this, &MainWindow::onBrowseExportFolder);
    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::onExportMod);
    connect(m_textureSetPanel, &TextureSetPanel::textureSetSelected, this, &MainWindow::onTextureSetSelected);
    connect(m_textureSetPanel, &TextureSetPanel::addRequested, this, &MainWindow::onAddTextureSet);
    connect(m_textureSetPanel, &TextureSetPanel::renameRequested, this, &MainWindow::onRenameTextureSet);
    connect(m_textureSetPanel, &TextureSetPanel::removeRequested, this, &MainWindow::onRemoveTextureSet);
    connect(m_slotEditor, &SlotEditorWidget::importRequested, this, &MainWindow::onImportTexture);
    connect(m_slotEditor, &SlotEditorWidget::importChannelRequested, this, &MainWindow::onImportChannel);
    connect(m_slotEditor, &SlotEditorWidget::fileDroppedOnSlot, this, &MainWindow::onDroppedOnSlot);
    connect(m_slotEditor, &SlotEditorWidget::fileDroppedOnChannel, this, &MainWindow::onDroppedOnChannel);
    connect(m_slotEditor, &SlotEditorWidget::slotPreviewRequested, this, &MainWindow::onSlotPreviewRequested);
    connect(m_slotEditor, &SlotEditorWidget::channelPreviewRequested, this, &MainWindow::onChannelPreviewRequested);
    connect(m_slotEditor, &SlotEditorWidget::slotCleared, this, &MainWindow::onSlotCleared);
    connect(m_slotEditor, &SlotEditorWidget::channelCleared, this, &MainWindow::onChannelCleared);
    connect(m_slotEditor, &SlotEditorWidget::rmaosSourceModeChanged, this, &MainWindow::onRmaosSourceModeChanged);
    connect(m_slotEditor, &SlotEditorWidget::matchTextureChanged, this, &MainWindow::onMatchTextureChanged);
    connect(m_slotEditor, &SlotEditorWidget::matchTextureModeChanged, this, &MainWindow::onMatchTextureModeChanged);
    connect(m_slotEditor, &SlotEditorWidget::exportCompressionChanged, this, &MainWindow::onExportCompressionChanged);
    connect(m_slotEditor, &SlotEditorWidget::exportSizeChanged, this, &MainWindow::onExportSizeChanged);
    connect(m_featurePanel, &FeatureTogglePanel::featuresChanged, this, &MainWindow::onFeaturesChanged);
    connect(m_paramPanel, &ParameterPanel::parametersChanged, this, &MainWindow::onParametersChanged);
    connect(m_slotEditor, &SlotEditorWidget::landscapeEdidsChanged, this, &MainWindow::onLandscapeEdidsChanged);
    connect(m_slotEditor, &SlotEditorWidget::slotPathOverrideChanged, this, &MainWindow::onSlotPathOverrideChanged);
}

void MainWindow::setupStatusBar()
{
    statusBar()->showMessage(tr("Ready"));
}

// ─── 3D Preview Settings Persistence ──────────────────────

void MainWindow::save3DPreviewSettings()
{
    auto& s = AppSettings::instance();
    s.setValue("Preview3D/lightIntensity", m_lightIntensitySlider->value());
    s.setValue("Preview3D/lightColorR", static_cast<double>(m_lightColorR));
    s.setValue("Preview3D/lightColorG", static_cast<double>(m_lightColorG));
    s.setValue("Preview3D/lightColorB", static_cast<double>(m_lightColorB));
    s.setValue("Preview3D/exposure", m_exposureSlider->value());
    s.setValue("Preview3D/hdriIndex", m_hdriCombo->currentIndex());
    s.setValue("Preview3D/iblIntensity", m_iblIntensitySlider->value());
    s.setValue("Preview3D/iblResIndex", m_iblResCombo->currentIndex());
    s.setValue("Preview3D/iblSamplesIndex", m_iblSamplesCombo->currentIndex());
    s.setValue("Preview3D/horizonOcclusion", m_horizonOcclusionCB->isChecked());
    s.setValue("Preview3D/multiBounceAO", m_multiBounceAOCB->isChecked());
    s.setValue("Preview3D/specularOcclusion", m_specularOcclusionCB->isChecked());
    s.setValue("Preview3D/vsync", m_vsyncCB->isChecked());
    s.setValue("Preview3D/taa", m_taaCB->isChecked());
    s.setValue("Preview3D/paperWhite", m_paperWhiteSlider->value());
    s.setValue("Preview3D/peakBrightness", m_peakBrightnessSlider->value());
    s.setValue("Preview3D/shapeIndex", m_shapeCombo->currentIndex());
}

void MainWindow::restore3DPreviewSettings()
{
    auto& s = AppSettings::instance();

    // Only restore if settings were previously saved
    if (!s.value("Preview3D/lightIntensity").isValid())
        return;

    m_lightIntensitySlider->setValue(s.value("Preview3D/lightIntensity", 30).toInt());
    m_exposureSlider->setValue(s.value("Preview3D/exposure", 0).toInt());

    float r = s.value("Preview3D/lightColorR", 1.0).toFloat();
    float g = s.value("Preview3D/lightColorG", 1.0).toFloat();
    float b = s.value("Preview3D/lightColorB", 1.0).toFloat();
    m_lightColorR = r;
    m_lightColorG = g;
    m_lightColorB = b;
    QColor lightColor = QColor::fromRgbF(r, g, b);
    m_lightColorBtn->setStyleSheet(
        QString("QPushButton { background: %1; border: 1px solid #888; }").arg(lightColor.name()));

    m_iblIntensitySlider->setValue(s.value("Preview3D/iblIntensity", 10).toInt());

    int iblResIdx = s.value("Preview3D/iblResIndex", 3).toInt();
    if (iblResIdx >= 0 && iblResIdx < m_iblResCombo->count())
        m_iblResCombo->setCurrentIndex(iblResIdx);

    int iblSamplesIdx = s.value("Preview3D/iblSamplesIndex", 2).toInt();
    if (iblSamplesIdx >= 0 && iblSamplesIdx < m_iblSamplesCombo->count())
        m_iblSamplesCombo->setCurrentIndex(iblSamplesIdx);

    m_horizonOcclusionCB->setChecked(s.value("Preview3D/horizonOcclusion", true).toBool());
    m_multiBounceAOCB->setChecked(s.value("Preview3D/multiBounceAO", true).toBool());
    m_specularOcclusionCB->setChecked(s.value("Preview3D/specularOcclusion", true).toBool());
    m_vsyncCB->setChecked(s.value("Preview3D/vsync", true).toBool());
    m_taaCB->setChecked(s.value("Preview3D/taa", true).toBool());
    m_paperWhiteSlider->setValue(s.value("Preview3D/paperWhite", 200).toInt());
    m_peakBrightnessSlider->setValue(s.value("Preview3D/peakBrightness", 1000).toInt());

    int shapeIdx = s.value("Preview3D/shapeIndex", 0).toInt();
    if (shapeIdx >= 0 && shapeIdx < m_shapeCombo->count())
        m_shapeCombo->setCurrentIndex(shapeIdx);

    // Restore HDRI last — it triggers loading which depends on IBL params being set
    int hdriIdx = s.value("Preview3D/hdriIndex", 0).toInt();
    if (hdriIdx >= 0 && hdriIdx < m_hdriCombo->count())
        m_hdriCombo->setCurrentIndex(hdriIdx);
}

// ─── Actions ───────────────────────────────────────────────

void MainWindow::onNewProject()
{
    m_project = Project{};
    m_project.name = "Untitled";
    m_projectFilePath.clear();
    m_currentSetIndex = -1;
    refreshUI();
    statusBar()->showMessage(tr("New project created"));
}

void MainWindow::onOpenProject()
{
    auto path = QFileDialog::getOpenFileName(this, tr("Open Project"), AppSettings::instance().lastProjectDir(),
                                             tr("TruePBR Project (*.tpbr);;All Files (*)"));
    if (path.isEmpty())
        return;

    AppSettings::instance().setLastProjectDir(QFileInfo(path).absolutePath());
    AppSettings::instance().addRecentProject(path);
    m_project = Project::load(path.toStdString());
    m_projectFilePath = path.toStdString();
    m_currentSetIndex = -1;
    rebuildRecentProjectsMenu();
    refreshUI();
    statusBar()->showMessage(tr("Opened: %1").arg(path));
}

void MainWindow::onSaveProject()
{
    if (!m_projectFilePath.empty())
    {
        saveProjectToPath(QString::fromStdString(m_projectFilePath.string()));
        return;
    }

    const QString path = promptProjectSavePath();
    if (path.isEmpty())
    {
        return;
    }

    saveProjectToPath(path);
}

void MainWindow::onSaveProjectAs()
{
    const QString path = promptProjectSavePath();
    if (path.isEmpty())
    {
        return;
    }

    saveProjectToPath(path);
}

void MainWindow::onRenameProject()
{
    bool ok = false;
    const QString currentName = defaultProjectName(m_project);
    const QString newName =
        QInputDialog::getText(this, tr("Project Name"), tr("Project name:"), QLineEdit::Normal, currentName, &ok)
            .trimmed();

    if (!ok || newName.isEmpty())
    {
        return;
    }

    m_project.name = newName.toStdString();
    refreshUI();
    statusBar()->showMessage(tr("Project renamed to: %1").arg(newName));
}

QString MainWindow::promptProjectSavePath() const
{
    QString suggestedPath;
    if (!m_projectFilePath.empty())
    {
        suggestedPath = QString::fromStdString(m_projectFilePath.string());
    }
    else
    {
        QString dir = AppSettings::instance().lastProjectDir();
        if (dir.isEmpty())
            suggestedPath = defaultProjectName(m_project) + QStringLiteral(".tpbr");
        else
            suggestedPath = dir + "/" + defaultProjectName(m_project) + QStringLiteral(".tpbr");
    }

    QString path = QFileDialog::getSaveFileName(const_cast<MainWindow*>(this), tr("Save Project"), suggestedPath,
                                                tr("TruePBR Project (*.tpbr)"));

    if (path.isEmpty())
    {
        return {};
    }

    if (!path.endsWith(QStringLiteral(".tpbr"), Qt::CaseInsensitive))
    {
        path += QStringLiteral(".tpbr");
    }

    return path;
}

bool MainWindow::saveProjectToPath(const QString& path)
{
    if (isPlaceholderProjectName(m_project))
    {
        m_project.name = QFileInfo(path).completeBaseName().toStdString();
    }

    if (!m_project.save(path.toStdString()))
    {
        QMessageBox::warning(this, tr("Save Project"), tr("Failed to save project."));
        return false;
    }

    m_projectFilePath = path.toStdString();
    AppSettings::instance().setLastProjectDir(QFileInfo(path).absolutePath());
    AppSettings::instance().addRecentProject(path);
    rebuildRecentProjectsMenu();
    refreshUI();
    statusBar()->showMessage(tr("Saved: %1").arg(path));
    return true;
}

void MainWindow::onExportMod()
{
    onExportPathEdited();

    if (m_project.textureSets.empty())
    {
        QMessageBox::information(this, tr("Export"), tr("Add at least one Texture Set before exporting."));
        return;
    }

    if (m_project.outputModFolder.empty())
    {
        QMessageBox::warning(this, tr("Export"), tr("Please choose an export folder first."));
        return;
    }

    // Run validation on all texture sets
    bool hasErrors = false;
    QString validationReport;
    for (size_t i = 0; i < m_project.textureSets.size(); ++i)
    {
        auto issues = TextureSetValidator::validate(m_project.textureSets[i]);
        if (!issues.empty())
        {
            validationReport += tr("Texture Set \"%1\":\n").arg(QString::fromStdString(m_project.textureSets[i].name));
            for (const auto& issue : issues)
            {
                bool isError = (issue.severity == ValidationSeverity::Error);
                if (isError)
                    hasErrors = true;
                validationReport +=
                    tr("  %1 %2\n").arg(isError ? "[ERROR]" : "[WARNING]").arg(QString::fromStdString(issue.message));
            }
            validationReport += "\n";
        }
    }

    if (hasErrors)
    {
        QMessageBox::warning(this, tr("Export - Validation Errors"),
                             tr("Export cannot proceed due to errors:\n\n%1").arg(validationReport));
        return;
    }

    if (!validationReport.isEmpty())
    {
        auto reply = QMessageBox::warning(this, tr("Export - Warnings"),
                                          tr("There are warnings:\n\n%1\nContinue with export?").arg(validationReport),
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply != QMessageBox::Yes)
            return;
    }

    // Run export on a worker thread with a progress dialog
    int totalSteps = static_cast<int>(m_project.textureSets.size()) + 2;
    auto* progressDialog = new QProgressDialog(tr("Exporting..."), tr("Cancel"), 0, totalSteps, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAutoReset(false);
    progressDialog->setAutoClose(false);
    progressDialog->setMinimumDuration(0);
    progressDialog->setValue(0);
    progressDialog->show();

    // Shared state between worker thread and UI
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    auto result = std::make_shared<std::atomic<bool>>(false);

    // Pre-translate the progress label format on the UI thread
    const QString exportingFmt = tr("Exporting: %1");

    // Take a copy of the project for the worker thread
    auto projectCopy = std::make_shared<Project>(m_project);

    auto* thread = QThread::create(
        [projectCopy, cancelled, result, progressDialog, exportingFmt]()
        {
            bool ok = ModExporter::exportMod(
                *projectCopy,
                [cancelled, progressDialog, exportingFmt](int current, int total, const std::string& desc)
                {
                    if (cancelled->load())
                        return false;
                    QMetaObject::invokeMethod(
                        progressDialog,
                        [progressDialog, current, total, desc, exportingFmt]()
                        {
                            progressDialog->setMaximum(total);
                            progressDialog->setValue(current);
                            if (!desc.empty())
                                progressDialog->setLabelText(exportingFmt.arg(QString::fromStdString(desc)));
                        },
                        Qt::QueuedConnection);
                    return !cancelled->load();
                });
            result->store(ok);
        });

    connect(progressDialog, &QProgressDialog::canceled, this, [cancelled]() { cancelled->store(true); });

    connect(thread, &QThread::finished, this,
            [this, thread, progressDialog, cancelled, result]()
            {
                // Disconnect canceled signal BEFORE close(), because
                // QProgressDialog::close() emits canceled.
                progressDialog->disconnect();
                progressDialog->close();
                progressDialog->deleteLater();
                thread->deleteLater();

                if (cancelled->load())
                {
                    statusBar()->showMessage(tr("Export cancelled"));
                }
                else if (result->load())
                {
                    QMessageBox::information(this, tr("Export"), tr("Export successful!"));
                    AppSettings::instance().setLastExportPath(
                        QString::fromStdString(m_project.outputModFolder.string()));
                }
                else
                {
                    QMessageBox::warning(this, tr("Export"), tr("Export failed. Check log for details."));
                }
            });

    thread->start();
}

void MainWindow::onBrowseExportFolder()
{
    const QString currentPath = m_exportPathEdit ? m_exportPathEdit->text() : QString();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Mod Folder"), currentPath);

    if (dir.isEmpty())
    {
        return;
    }

    m_project.outputModFolder = dir.toStdString();
    if (m_exportPathEdit)
    {
        m_exportPathEdit->setText(dir);
    }
    statusBar()->showMessage(tr("Export folder set: %1").arg(dir));
}

void MainWindow::onExportPathEdited()
{
    if (!m_exportPathEdit)
    {
        return;
    }

    m_project.outputModFolder = m_exportPathEdit->text().trimmed().toStdString();
}

void MainWindow::onTextureSetSelected(int index)
{
    m_currentSetIndex = index;
    if (index >= 0 && index < static_cast<int>(m_project.textureSets.size()))
    {
        const auto& ts = m_project.textureSets[index];
        m_slotEditor->setTextureSet(ts);
        m_featurePanel->setFeatures(ts.features);
        m_paramPanel->setParameters(ts.params, ts.features);
    }

    updateEditorState();
    refreshPreview();
}

void MainWindow::onAddTextureSet()
{
    bool ok = false;
    auto name = QInputDialog::getText(this, tr("New Texture Set"), tr("Name:"), QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty())
        return;

    auto match = QInputDialog::getText(this, tr("Vanilla Texture Match"),
                                       tr("Vanilla albedo path (e.g. architecture\\whiterun\\wrwoodplank01):"),
                                       QLineEdit::Normal, "", &ok);
    if (!ok || match.isEmpty())
        return;

    m_project.addTextureSet(name.toStdString(), match.toStdString());
    m_currentSetIndex = static_cast<int>(m_project.textureSets.size()) - 1;
    refreshUI();
    onTextureSetSelected(m_currentSetIndex);

    statusBar()->showMessage(tr("Added texture set: %1").arg(name));
}

void MainWindow::onRenameTextureSet(int index)
{
    if (index < 0 || index >= static_cast<int>(m_project.textureSets.size()))
    {
        return;
    }

    bool ok = false;
    const QString currentName = QString::fromStdString(m_project.textureSets[index].name);
    const QString newName = QInputDialog::getText(this, tr("Rename Texture Set"), tr("Texture Set name:"),
                                                  QLineEdit::Normal, currentName, &ok)
                                .trimmed();

    if (!ok || newName.isEmpty() || newName == currentName)
    {
        return;
    }

    m_project.textureSets[index].name = newName.toStdString();
    refreshUI();
    if (index == m_currentSetIndex)
    {
        onTextureSetSelected(index);
    }
    statusBar()->showMessage(tr("Renamed texture set to: %1").arg(newName));
}

void MainWindow::onRemoveTextureSet(int index)
{
    if (index < 0 || index >= static_cast<int>(m_project.textureSets.size()))
        return;

    auto reply = QMessageBox::question(
        this, tr("Remove"),
        tr("Remove texture set '%1'?").arg(QString::fromStdString(m_project.textureSets[index].name)));

    if (reply == QMessageBox::Yes)
    {
        m_project.removeTextureSet(index);
        if (m_project.textureSets.empty())
        {
            m_currentSetIndex = -1;
        }
        else if (index >= static_cast<int>(m_project.textureSets.size()))
        {
            m_currentSetIndex = static_cast<int>(m_project.textureSets.size()) - 1;
        }
        else
        {
            m_currentSetIndex = index;
        }
        refreshUI();
        if (m_currentSetIndex >= 0)
        {
            onTextureSetSelected(m_currentSetIndex);
        }
        else
        {
            updateEditorState();
            refreshPreview();
        }
    }
}

void MainWindow::onImportTexture(PBRTextureSlot slot)
{
    if (m_currentSetIndex < 0)
        return;

    auto path = QFileDialog::getOpenFileName(this, tr("Import %1").arg(slotDisplayName(slot)), QString(),
                                             TextureImporter::fileFilter());

    if (path.isEmpty())
        return;

    auto entry = TextureImporter::importTexture(path.toStdString(), slot);
    if (slot == PBRTextureSlot::RMAOS)
    {
        m_project.textureSets[m_currentSetIndex].rmaosSourceMode = RMAOSSourceMode::PackedTexture;
    }
    m_project.textureSets[m_currentSetIndex].textures[slot] = entry;

    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
    refreshPreview();
    statusBar()->showMessage(tr("Imported %1: %2 (%3x%4)")
                                 .arg(slotDisplayName(slot))
                                 .arg(QString::fromStdString(entry.sourcePath.filename().string()))
                                 .arg(entry.width)
                                 .arg(entry.height));
}

void MainWindow::onImportChannel(ChannelMap channel)
{
    if (m_currentSetIndex < 0)
        return;

    const char* name = channelDisplayName(channel);

    auto path =
        QFileDialog::getOpenFileName(this, tr("Import %1 Channel").arg(name), QString(), TextureImporter::fileFilter());

    if (path.isEmpty())
        return;

    auto entry = TextureImporter::importChannelMap(path.toStdString(), channel);
    m_project.textureSets[m_currentSetIndex].rmaosSourceMode = RMAOSSourceMode::SeparateChannels;
    m_project.textureSets[m_currentSetIndex].channelMaps[channel] = entry;

    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
    refreshPreview();
    statusBar()->showMessage(tr("Imported %1 channel: %2 (%3x%4)")
                                 .arg(name)
                                 .arg(QString::fromStdString(entry.sourcePath.filename().string()))
                                 .arg(entry.width)
                                 .arg(entry.height));
}

void MainWindow::onBatchImport()
{
    if (m_currentSetIndex < 0)
    {
        QMessageBox::information(this, tr("Batch Import"), tr("Select or create a Texture Set first."));
        return;
    }

    auto dir = QFileDialog::getExistingDirectory(this, tr("Select Folder for Batch Import"));
    if (dir.isEmpty())
        return;

    auto scanResult = TextureImporter::scanFolder(dir.toStdString());

    auto& ts = m_project.textureSets[m_currentSetIndex];
    int imported = 0;

    // Import slot files
    for (const auto& [slot, path] : scanResult.slotFiles)
    {
        auto entry = TextureImporter::importTexture(path, slot);
        ts.textures[slot] = entry;
        ++imported;
    }

    // Import channel files
    for (const auto& [ch, path] : scanResult.channelFiles)
    {
        auto entry = TextureImporter::importChannelMap(path, ch);
        ts.channelMaps[ch] = entry;
        ++imported;
    }

    // If we found individual channels, switch to split mode
    if (!scanResult.channelFiles.empty())
    {
        ts.rmaosSourceMode = RMAOSSourceMode::SeparateChannels;
    }

    m_slotEditor->setTextureSet(ts);
    m_featurePanel->setFeatures(ts.features);
    refreshPreview();

    QString msg = tr("Batch import: %1 files imported").arg(imported);
    if (!scanResult.unmatched.empty())
    {
        msg += tr(", %1 unmatched").arg(static_cast<int>(scanResult.unmatched.size()));
    }
    statusBar()->showMessage(msg);

    if (imported == 0)
    {
        QMessageBox::information(this, tr("Batch Import"),
                                 tr("No matching textures found in the selected folder.\n\n"
                                    "Expected suffixes: _n, _rmaos, _g, _p, _s, _f, _cnr, "
                                    "_roughness, _metallic, _ao, _specular"));
    }
}

void MainWindow::onDroppedOnSlot(PBRTextureSlot slot, const QString& filePath)
{
    if (m_currentSetIndex < 0)
        return;

    auto entry = TextureImporter::importTexture(filePath.toStdString(), slot);
    if (slot == PBRTextureSlot::RMAOS)
    {
        m_project.textureSets[m_currentSetIndex].rmaosSourceMode = RMAOSSourceMode::PackedTexture;
    }
    m_project.textureSets[m_currentSetIndex].textures[slot] = entry;

    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
    refreshPreview();
    statusBar()->showMessage(tr("Dropped %1: %2 (%3x%4)")
                                 .arg(slotDisplayName(slot))
                                 .arg(QString::fromStdString(entry.sourcePath.filename().string()))
                                 .arg(entry.width)
                                 .arg(entry.height));
}

void MainWindow::onDroppedOnChannel(ChannelMap channel, const QString& filePath)
{
    if (m_currentSetIndex < 0)
        return;

    const char* name = channelDisplayName(channel);

    auto entry = TextureImporter::importChannelMap(filePath.toStdString(), channel);
    m_project.textureSets[m_currentSetIndex].rmaosSourceMode = RMAOSSourceMode::SeparateChannels;
    m_project.textureSets[m_currentSetIndex].channelMaps[channel] = entry;

    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
    refreshPreview();
    statusBar()->showMessage(tr("Dropped %1 channel: %2 (%3x%4)")
                                 .arg(name)
                                 .arg(QString::fromStdString(entry.sourcePath.filename().string()))
                                 .arg(entry.width)
                                 .arg(entry.height));
}

void MainWindow::onSlotPreviewRequested(PBRTextureSlot slot)
{
    if (m_currentSetIndex < 0 || m_currentSetIndex >= static_cast<int>(m_project.textureSets.size()))
        return;

    const auto& ts = m_project.textureSets[m_currentSetIndex];
    auto it = ts.textures.find(slot);
    if (it == ts.textures.end() || it->second.sourcePath.empty())
    {
        statusBar()->showMessage(tr("No texture assigned to %1").arg(slotDisplayName(slot)));
        return;
    }

    const QImage image = loadPreviewImage(it->second.sourcePath);
    if (image.isNull())
    {
        statusBar()->showMessage(tr("Failed to load preview for %1").arg(slotDisplayName(slot)));
        return;
    }

    m_previewWidget->setImage(image);
    // Show channel selector for multi-channel data textures (RMAOS, CoatNormalRoughness, Fuzz, Subsurface)
    bool showChannels =
        (slot == PBRTextureSlot::RMAOS || slot == PBRTextureSlot::CoatNormalRoughness || slot == PBRTextureSlot::Fuzz ||
         slot == PBRTextureSlot::Subsurface || slot == PBRTextureSlot::Diffuse);
    m_previewWidget->setChannelSelectorVisible(showChannels);

    if (m_previewStack != nullptr)
    {
        m_previewStack->setCurrentWidget(m_previewWidget);
    }
    statusBar()->showMessage(tr("Previewing: %1 (%2)")
                                 .arg(slotDisplayName(slot))
                                 .arg(QString::fromStdString(it->second.sourcePath.filename().string())));
}

void MainWindow::onChannelPreviewRequested(ChannelMap channel)
{
    if (m_currentSetIndex < 0 || m_currentSetIndex >= static_cast<int>(m_project.textureSets.size()))
        return;

    const auto& ts = m_project.textureSets[m_currentSetIndex];
    auto it = ts.channelMaps.find(channel);
    if (it == ts.channelMaps.end() || it->second.sourcePath.empty())
    {
        statusBar()->showMessage(tr("No image assigned to %1 channel").arg(channelDisplayName(channel)));
        return;
    }

    const QImage image = loadPreviewImage(it->second.sourcePath);
    if (image.isNull())
    {
        statusBar()->showMessage(tr("Failed to load preview for %1 channel").arg(channelDisplayName(channel)));
        return;
    }

    m_previewWidget->setImage(image);
    m_previewWidget->setChannelSelectorVisible(true);

    if (m_previewStack != nullptr)
    {
        m_previewStack->setCurrentWidget(m_previewWidget);
    }
    statusBar()->showMessage(tr("Previewing: %1 (%2)")
                                 .arg(channelDisplayName(channel))
                                 .arg(QString::fromStdString(it->second.sourcePath.filename().string())));
}

void MainWindow::onSlotCleared(PBRTextureSlot slot)
{
    if (m_currentSetIndex < 0)
        return;

    auto& ts = m_project.textureSets[m_currentSetIndex];
    ts.textures.erase(slot);

    m_slotEditor->setTextureSet(ts);
    refreshPreview();
    statusBar()->showMessage(tr("Cleared %1 texture").arg(slotDisplayName(slot)));
}

void MainWindow::onChannelCleared(ChannelMap channel)
{
    if (m_currentSetIndex < 0)
        return;

    auto& ts = m_project.textureSets[m_currentSetIndex];
    ts.channelMaps.erase(channel);

    m_slotEditor->setTextureSet(ts);
    refreshPreview();
    statusBar()->showMessage(tr("Cleared %1 channel").arg(channelDisplayName(channel)));
}

void MainWindow::onRmaosSourceModeChanged(RMAOSSourceMode mode)
{
    if (m_currentSetIndex < 0)
        return;

    m_project.textureSets[m_currentSetIndex].rmaosSourceMode = mode;
    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
}

void MainWindow::onMatchTextureChanged(const QString& newPath)
{
    if (m_currentSetIndex < 0)
        return;
    m_project.textureSets[m_currentSetIndex].matchTexture = newPath.toStdString();
    spdlog::debug("Match texture updated: {}", newPath.toStdString());
}

void MainWindow::onMatchTextureModeChanged(TextureMatchMode mode)
{
    if (m_currentSetIndex < 0)
        return;
    m_project.textureSets[m_currentSetIndex].matchMode = mode;
    spdlog::debug("Match texture mode updated: {}", textureMatchModeKey(mode));
}

void MainWindow::onExportCompressionChanged(PBRTextureSlot slot, DDSCompressionMode mode)
{
    if (m_currentSetIndex < 0)
        return;

    m_project.textureSets[m_currentSetIndex].exportCompression[slot] = mode;
    spdlog::debug("Export compression updated: {} -> {}", slotDisplayName(slot), compressionModeKey(mode));
}

void MainWindow::onExportSizeChanged(PBRTextureSlot slot, int width, int height)
{
    if (m_currentSetIndex < 0)
        return;

    auto& ts = m_project.textureSets[m_currentSetIndex];

    // Determine the native size for this slot
    int nativeW = 0, nativeH = 0;
    if (slot == PBRTextureSlot::RMAOS && ts.rmaosSourceMode == RMAOSSourceMode::SeparateChannels)
    {
        for (const auto& [ch, chEntry] : ts.channelMaps)
        {
            if (chEntry.width > 0 && chEntry.height > 0 &&
                static_cast<int64_t>(chEntry.width) * chEntry.height > static_cast<int64_t>(nativeW) * nativeH)
            {
                nativeW = chEntry.width;
                nativeH = chEntry.height;
            }
        }
    }
    else
    {
        auto texIt = ts.textures.find(slot);
        if (texIt != ts.textures.end())
        {
            nativeW = texIt->second.width;
            nativeH = texIt->second.height;
        }
    }

    if (width == nativeW && height == nativeH)
    {
        // Original/native size selected — remove override
        ts.exportSize.erase(slot);
    }
    else
    {
        ts.exportSize[slot] = {width, height};
    }
    spdlog::debug("Export size updated: {} -> {}x{}", slotDisplayName(slot), width, height);
}

void MainWindow::onFeaturesChanged(const PBRFeatureFlags& flags)
{
    if (m_currentSetIndex < 0)
        return;
    m_project.textureSets[m_currentSetIndex].features = flags;
    m_slotEditor->updateSlots(flags);
    m_paramPanel->setParameters(m_project.textureSets[m_currentSetIndex].params, flags);
    m_materialPreview->setFeatureParams(flags, m_project.textureSets[m_currentSetIndex].params);
}

void MainWindow::onParametersChanged(const PBRParameters& params)
{
    if (m_currentSetIndex < 0)
        return;
    m_project.textureSets[m_currentSetIndex].params = params;
    m_materialPreview->setMaterialParams(params.specularLevel, params.roughnessScale);
    m_materialPreview->setFeatureParams(m_project.textureSets[m_currentSetIndex].features, params);
}

void MainWindow::onLandscapeEdidsChanged(const QStringList& edids)
{
    if (m_currentSetIndex < 0)
        return;

    auto& ts = m_project.textureSets[m_currentSetIndex];
    ts.landscapeEdids.clear();
    for (const auto& edid : edids)
    {
        ts.landscapeEdids.push_back(edid.toStdString());
    }
    spdlog::debug("Landscape EDIDs updated: {} entries", ts.landscapeEdids.size());
}

void MainWindow::onSlotPathOverrideChanged(PBRTextureSlot slot, const QString& path)
{
    if (m_currentSetIndex < 0)
        return;

    auto& ts = m_project.textureSets[m_currentSetIndex];
    if (path.isEmpty())
    {
        ts.slotPathOverrides.erase(slot);
    }
    else
    {
        ts.slotPathOverrides[slot] = path.toStdString();
    }
    spdlog::debug("Slot path override updated: {} -> {}", slotDisplayName(slot), path.toStdString());
}

// ─── Refresh ───────────────────────────────────────────────

void MainWindow::refreshUI()
{
    m_textureSetPanel->setTextureSets(m_project.textureSets);
    if (m_currentSetIndex >= 0 && m_currentSetIndex < static_cast<int>(m_project.textureSets.size()))
    {
        m_textureSetPanel->setCurrentIndex(m_currentSetIndex);
    }
    else
    {
        m_textureSetPanel->setCurrentIndex(-1);
    }
    setWindowTitle(tr("TruePBR Manager - %1").arg(defaultProjectName(m_project)));

    if (m_exportPathEdit)
    {
        const QString exportPath = QString::fromStdString(m_project.outputModFolder.string());
        if (m_exportPathEdit->text() != exportPath)
        {
            m_exportPathEdit->setText(exportPath);
        }
    }

    if (m_exportBtn)
    {
        m_exportBtn->setEnabled(!m_project.textureSets.empty());
    }

    updateEditorState();

    if (m_currentSetIndex < 0 || m_currentSetIndex >= static_cast<int>(m_project.textureSets.size()))
    {
        m_previewWidget->clear();
        if (m_previewStack != nullptr)
        {
            m_previewStack->setCurrentWidget(m_previewPlaceholder);
        }
    }
}

void MainWindow::updateEditorState()
{
    const bool hasSelection =
        m_currentSetIndex >= 0 && m_currentSetIndex < static_cast<int>(m_project.textureSets.size());

    if (m_editorStack != nullptr)
    {
        if (hasSelection)
        {
            m_editorStack->setCurrentIndex(0);
        }
        else
        {
            const bool hasAnyTextureSet = !m_project.textureSets.empty();
            m_editorPlaceholder->setText(
                hasAnyTextureSet ? tr("Select a Texture Set from the list on the left to edit its textures, features, "
                                      "and parameters.")
                                 : tr("No Texture Sets yet.\n\nUse Add on the left to create your first Texture Set."));
            m_editorStack->setCurrentWidget(m_editorPlaceholder);
        }
    }

    if (!hasSelection && m_previewStack != nullptr)
    {
        const bool hasAnyTextureSet = !m_project.textureSets.empty();
        m_previewPlaceholder->setText(hasAnyTextureSet
                                          ? tr("Select a Texture Set to see its texture preview.")
                                          : tr("Preview will appear here after you create and select a Texture Set."));
        m_previewStack->setCurrentWidget(m_previewPlaceholder);
    }
}

void MainWindow::refreshPreview()
{
    if (m_preview3DMode)
    {
        refresh3DPreview();
        return;
    }

    if (m_currentSetIndex < 0 || m_currentSetIndex >= static_cast<int>(m_project.textureSets.size()))
    {
        m_previewWidget->clear();
        if (m_previewStack != nullptr)
        {
            m_previewStack->setCurrentWidget(m_previewPlaceholder);
        }
        return;
    }

    const auto& textureSet = m_project.textureSets[m_currentSetIndex];

    const auto tryShowTexture = [this](PBRTextureSlot slot, const PBRTextureSet& set)
    {
        auto it = set.textures.find(slot);
        if (it == set.textures.end() || it->second.sourcePath.empty())
        {
            return false;
        }

        const QImage image = loadPreviewImage(it->second.sourcePath);
        if (image.isNull())
        {
            return false;
        }

        m_previewWidget->setImage(image);
        if (m_previewStack != nullptr)
        {
            m_previewStack->setCurrentWidget(m_previewWidget);
        }
        return true;
    };

    if (tryShowTexture(PBRTextureSlot::Diffuse, textureSet))
    {
        return;
    }

    if (tryShowTexture(PBRTextureSlot::Normal, textureSet))
    {
        return;
    }

    m_previewWidget->clear();
    if (m_previewStack != nullptr)
    {
        m_previewPlaceholder->setText(tr("No previewable texture has been imported for the selected Texture Set yet."));
        m_previewStack->setCurrentWidget(m_previewPlaceholder);
    }
}

void MainWindow::pushAllPreviewSettings()
{
    // Re-apply all 3D preview UI state to the renderer.
    // This is needed because restore3DPreviewSettings() runs at construction
    // time when the renderer doesn't exist yet, so the slider signals fire
    // but the values are silently dropped.

    float intensity = static_cast<float>(m_lightIntensitySlider->value()) / 10.0f;
    m_materialPreview->setLightIntensity(intensity);
    m_materialPreview->setLightColor(m_lightColorR, m_lightColorG, m_lightColorB);

    float ev = static_cast<float>(m_exposureSlider->value()) / 10.0f;
    m_materialPreview->setExposure(ev);

    uint32_t flags = 0;
    if (m_horizonOcclusionCB->isChecked())
        flags |= 1u;
    if (m_multiBounceAOCB->isChecked())
        flags |= 2u;
    if (m_specularOcclusionCB->isChecked())
        flags |= 4u;
    m_materialPreview->setRenderFlags(flags);

    m_materialPreview->setVSync(m_vsyncCB->isChecked());
    m_materialPreview->setTAAEnabled(m_taaCB->isChecked());

    float iblIntensity = static_cast<float>(m_iblIntensitySlider->value()) / 10.0f;
    m_materialPreview->setIBLIntensity(iblIntensity);

    uint32_t debugMode = static_cast<uint32_t>(m_debugModeCombo->currentData().toInt());
    m_materialPreview->setDebugMode(debugMode);

    int shapeIdx = m_shapeCombo->currentIndex();
    if (shapeIdx >= 0)
    {
        auto shape = static_cast<PreviewShape>(m_shapeCombo->itemData(shapeIdx).toInt());
        m_materialPreview->setShape(shape);
    }
}

void MainWindow::refresh3DPreview()
{
    if (m_currentSetIndex < 0 || m_currentSetIndex >= static_cast<int>(m_project.textureSets.size()))
    {
        if (m_previewStack != nullptr)
        {
            m_previewStack->setCurrentWidget(m_previewPlaceholder);
        }
        return;
    }

    const auto& ts = m_project.textureSets[m_currentSetIndex];

    // Load RGBA pixel data for each PBR texture
    auto loadPixels = [](const PBRTextureSet& set, PBRTextureSlot slot, std::vector<uint8_t>& pixels, int& w,
                         int& h) -> bool
    {
        auto it = set.textures.find(slot);
        if (it == set.textures.end() || it->second.sourcePath.empty())
            return false;

        const auto ext = FileUtils::getExtensionLower(it->second.sourcePath);
        if (ext == ".dds")
        {
            return DDSUtils::loadDDS(it->second.sourcePath, w, h, pixels);
        }
        else
        {
            auto img = ImageUtils::loadImage(it->second.sourcePath);
            if (img.pixels.empty())
                return false;
            w = img.width;
            h = img.height;
            pixels = std::move(img.pixels);
            return true;
        }
    };

    std::vector<uint8_t> diffusePixels, normalPixels, rmaosPixels;
    int dw = 0, dh = 0, nw = 0, nh = 0, rw = 0, rh = 0;

    loadPixels(ts, PBRTextureSlot::Diffuse, diffusePixels, dw, dh);
    loadPixels(ts, PBRTextureSlot::Normal, normalPixels, nw, nh);

    // RMAOS: respect the authoring mode
    if (ts.rmaosSourceMode == RMAOSSourceMode::SeparateChannels && !ts.channelMaps.empty())
    {
        // Compose RMAOS from individual channel maps on the fly
        auto loadChannel = [&](ChannelMap ch, int targetW, int targetH, std::vector<uint8_t>& outChannel,
                               uint8_t defaultVal) -> bool
        {
            auto it = ts.channelMaps.find(ch);
            if (it == ts.channelMaps.end() || it->second.sourcePath.empty())
                return false;

            std::vector<uint8_t> rgba;
            int cw = 0, ch2 = 0;
            const auto ext = FileUtils::getExtensionLower(it->second.sourcePath);
            if (ext == ".dds")
            {
                if (!DDSUtils::loadDDS(it->second.sourcePath, cw, ch2, rgba))
                    return false;
            }
            else
            {
                auto img = ImageUtils::loadImage(it->second.sourcePath);
                if (img.pixels.empty())
                    return false;
                cw = img.width;
                ch2 = img.height;
                rgba = std::move(img.pixels);
            }

            // Extract R channel and resize if needed
            const size_t srcCount = static_cast<size_t>(cw) * ch2;
            outChannel.resize(static_cast<size_t>(targetW) * targetH);
            if (cw == targetW && ch2 == targetH)
            {
                for (size_t i = 0; i < srcCount; ++i)
                    outChannel[i] = rgba[i * 4];
            }
            else
            {
                for (int y = 0; y < targetH; ++y)
                {
                    int srcY = y * ch2 / targetH;
                    for (int x = 0; x < targetW; ++x)
                    {
                        int srcX = x * cw / targetW;
                        outChannel[y * targetW + x] = rgba[(srcY * cw + srcX) * 4];
                    }
                }
            }
            return true;
        };

        // Determine resolution from the first available channel
        rw = 0;
        rh = 0;
        for (const auto& [ch, entry] : ts.channelMaps)
        {
            if (!entry.sourcePath.empty() && entry.width > 0 && entry.height > 0)
            {
                rw = entry.width;
                rh = entry.height;
                break;
            }
        }

        if (rw > 0 && rh > 0)
        {
            const size_t pixelCount = static_cast<size_t>(rw) * rh;
            std::vector<uint8_t> roughness(pixelCount, 255);
            std::vector<uint8_t> metallic(pixelCount, 0);
            std::vector<uint8_t> ao(pixelCount, 255);
            std::vector<uint8_t> specular(pixelCount, 255);

            loadChannel(ChannelMap::Roughness, rw, rh, roughness, 255);
            loadChannel(ChannelMap::Metallic, rw, rh, metallic, 0);
            loadChannel(ChannelMap::AO, rw, rh, ao, 255);
            loadChannel(ChannelMap::Specular, rw, rh, specular, 255);

            rmaosPixels.resize(pixelCount * 4);
            for (size_t i = 0; i < pixelCount; ++i)
            {
                rmaosPixels[i * 4 + 0] = roughness[i];
                rmaosPixels[i * 4 + 1] = metallic[i];
                rmaosPixels[i * 4 + 2] = ao[i];
                rmaosPixels[i * 4 + 3] = specular[i];
            }
        }
    }
    else
    {
        loadPixels(ts, PBRTextureSlot::RMAOS, rmaosPixels, rw, rh);
    }

    m_materialPreview->setTextures(diffusePixels.empty() ? nullptr : diffusePixels.data(), dw, dh,
                                   normalPixels.empty() ? nullptr : normalPixels.data(), nw, nh,
                                   rmaosPixels.empty() ? nullptr : rmaosPixels.data(), rw, rh);

    m_materialPreview->setMaterialParams(ts.params.specularLevel, ts.params.roughnessScale);
    m_materialPreview->setFeatureParams(ts.features, ts.params);

    // Load feature textures
    {
        std::vector<uint8_t> emissivePixels, feat0Pixels, feat1Pixels;
        int ew = 0, eh = 0, f0w = 0, f0h = 0, f1w = 0, f1h = 0;

        loadPixels(ts, PBRTextureSlot::Emissive, emissivePixels, ew, eh);

        // Feature texture 0: CoatNormalRoughness or Fuzz
        if (ts.features.multilayer)
            loadPixels(ts, PBRTextureSlot::CoatNormalRoughness, feat0Pixels, f0w, f0h);
        else if (ts.features.fuzz)
            loadPixels(ts, PBRTextureSlot::Fuzz, feat0Pixels, f0w, f0h);

        // Feature texture 1: Subsurface or CoatColor
        if (ts.features.subsurface)
            loadPixels(ts, PBRTextureSlot::Subsurface, feat1Pixels, f1w, f1h);
        else if (ts.features.coatDiffuse)
            loadPixels(ts, PBRTextureSlot::CoatColor, feat1Pixels, f1w, f1h);

        m_materialPreview->setFeatureTextures(emissivePixels.empty() ? nullptr : emissivePixels.data(), ew, eh,
                                              feat0Pixels.empty() ? nullptr : feat0Pixels.data(), f0w, f0h,
                                              feat1Pixels.empty() ? nullptr : feat1Pixels.data(), f1w, f1h);
    }

    if (m_previewStack != nullptr)
    {
        m_previewStack->setCurrentWidget(m_materialPreview);
    }

    // Push all UI settings to the renderer (which may have just been initialized)
    pushAllPreviewSettings();

    statusBar()->showMessage(tr("3D Preview: %1").arg(QString::fromStdString(ts.name)));
}

// ─── Language Menu ─────────────────────────────────────────

void MainWindow::setupLanguageMenu()
{
    m_languageMenu = menuBar()->addMenu(tr("&Language"));
    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);

    rebuildLanguageMenu();

    auto& tm = TranslationManager::instance();
    connect(&tm, &TranslationManager::availableLanguagesChanged, this, &MainWindow::rebuildLanguageMenu);
}

void MainWindow::rebuildLanguageMenu()
{
    m_languageMenu->clear();

    auto& tm = TranslationManager::instance();
    const auto& langs = tm.availableLanguages();
    const QString current = tm.currentLocale();

    // Delete old action group and create a new one
    delete m_languageGroup;
    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);

    // English (always available, source strings)
    auto* enAction = m_languageMenu->addAction("English");
    enAction->setCheckable(true);
    enAction->setChecked(current.compare("en", Qt::CaseInsensitive) == 0);
    enAction->setData("en");
    m_languageGroup->addAction(enAction);

    for (const auto& lang : langs)
    {
        if (lang.locale.compare("en", Qt::CaseInsensitive) == 0)
            continue;

        auto* action = m_languageMenu->addAction(lang.name);
        action->setCheckable(true);
        action->setChecked(lang.locale.compare(current, Qt::CaseInsensitive) == 0);
        action->setData(lang.locale);
        m_languageGroup->addAction(action);
    }

    connect(m_languageGroup, &QActionGroup::triggered, this,
            [](QAction* action) { TranslationManager::instance().switchLanguage(action->data().toString()); });
}

// ─── Retranslation ─────────────────────────────────────────

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    // Window title
    setWindowTitle(tr("TruePBR Manager - %1").arg(defaultProjectName(m_project)));

    // File menu
    m_fileMenu->setTitle(tr("&File"));
    m_newProjectAction->setText(tr("&New Project"));
    m_openProjectAction->setText(tr("&Open Project"));
    m_recentMenu->setTitle(tr("Recent &Projects"));
    m_saveProjectAction->setText(tr("&Save Project"));
    m_saveAsAction->setText(tr("Save Project &As..."));
    m_projectNameAction->setText(tr("Project &Name..."));
    m_batchImportAction->setText(tr("&Batch Import Folder..."));
    m_exitAction->setText(tr("E&xit"));

    // Language menu title
    m_languageMenu->setTitle(tr("&Language"));

    // Export row
    m_exportLabel->setText(tr("Export Folder:"));
    m_exportPathEdit->setPlaceholderText(tr("Select the target mod folder"));
    m_exportBrowseBtn->setText(tr("Browse..."));
    m_exportBtn->setText(tr("Export Mod"));

    // Preview mode buttons
    m_preview2DBtn->setText(tr("2D"));
    m_preview3DBtn->setText(tr("3D"));

    // 3D control bar labels
    m_lightLabel->setText(tr("Light:"));
    m_lightIntensitySlider->setToolTip(tr("Light Intensity"));
    m_lightColorBtn->setToolTip(tr("Light Color"));
    m_evLabel->setText(tr("EV:"));
    m_exposureSlider->setToolTip(tr("Exposure Compensation (EV)"));

    // Render options
    m_horizonOcclusionCB->setText(tr("Horizon Occlusion"));
    m_horizonOcclusionCB->setToolTip(tr("Attenuate specular IBL below geometric horizon"));
    m_multiBounceAOCB->setText(tr("MultiBounce AO"));
    m_multiBounceAOCB->setToolTip(tr("Multi-bounce ambient occlusion (Jimenez 2016)"));
    m_specularOcclusionCB->setText(tr("Specular Occlusion"));
    m_specularOcclusionCB->setToolTip(tr("AO-based specular occlusion"));

    // Display options
    m_vsyncCB->setText(tr("VSync"));
    m_vsyncCB->setToolTip(tr("Vertical sync (off = uncapped frame rate with tearing support)"));
    m_taaCB->setText(tr("TAA"));
    m_taaCB->setToolTip(tr("Temporal Anti-Aliasing"));
    m_hdrCB->setText(tr("HDR"));
    m_hdrCB->setToolTip(tr("Enable HDR output (scRGB) — requires Windows HDR enabled"));
    m_paperWhiteTextLabel->setText(tr("Paper White:"));
    m_paperWhiteSlider->setToolTip(tr("Paper white brightness in nits (SDR reference white)"));
    m_peakTextLabel->setText(tr("Peak:"));
    m_peakBrightnessSlider->setToolTip(tr("Peak brightness in nits (0 = use display maximum)"));

    // IBL row
    m_hdriLabel->setText(tr("HDRI:"));
    m_hdriCombo->setItemText(0, tr("(None)"));
    m_iblLabel->setText(tr("IBL:"));
    m_iblIntensitySlider->setToolTip(tr("IBL Intensity"));

    // IBL params row
    m_prefilterResLabel->setText(tr("Prefilter Res:"));
    m_iblResCombo->setToolTip(tr("Prefiltered cubemap face resolution"));
    m_samplesLabel->setText(tr("Samples:"));
    m_iblSamplesCombo->setToolTip(tr("GGX importance samples per texel for specular prefiltering"));

    // Debug visualization combo
    m_debugModeCombo->setItemText(0, tr("Full Shading"));
    m_debugModeCombo->setItemText(1, tr("Normal"));
    m_debugModeCombo->setItemText(2, tr("Roughness"));
    m_debugModeCombo->setItemText(3, tr("Metallic"));
    m_debugModeCombo->setItemText(4, tr("AO"));
    m_debugModeCombo->setItemText(5, tr("Specular"));

    // Placeholders – update only if currently visible
    updateEditorState();
}

} // namespace tpbr
