#include "MainWindow.h"

#include "core/JsonExporter.h"
#include "core/ModExporter.h"
#include "core/TextureImporter.h"
#include "core/TextureSetValidator.h"
#include "ui/FeatureTogglePanel.h"
#include "ui/ParameterPanel.h"
#include "ui/SlotEditorWidget.h"
#include "ui/TexturePreviewWidget.h"
#include "ui/TextureSetPanel.h"
#include "ui/MaterialPreviewWidget.h"

#include "renderer/IBLProcessor.h"

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
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
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

    m_project.name = "Untitled";

    setupMenuBar();
    setupCentralWidget();
    setupStatusBar();
    refreshUI();
}

MainWindow::~MainWindow() = default;

// ─── Menu Bar ──────────────────────────────────────────────

void MainWindow::setupMenuBar()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New Project"), this, &MainWindow::onNewProject, QKeySequence::New);
    fileMenu->addAction(tr("&Open Project"), this, &MainWindow::onOpenProject, QKeySequence::Open);
    fileMenu->addAction(tr("&Save Project"), this, &MainWindow::onSaveProject, QKeySequence::Save);
    fileMenu->addAction(tr("Save Project &As..."), this, &MainWindow::onSaveProjectAs, QKeySequence::SaveAs);
    fileMenu->addAction(tr("Project &Name..."), this, &MainWindow::onRenameProject);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Batch Import Folder..."), this, &MainWindow::onBatchImport);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence::Quit);
}

// ─── Central Widget ────────────────────────────────────────

void MainWindow::setupCentralWidget()
{
    auto* rootWidget = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(rootWidget);

    auto* exportRow = new QHBoxLayout();
    auto* exportLabel = new QLabel(tr("Export Folder:"), rootWidget);
    exportLabel->setFixedWidth(100);
    m_exportPathEdit = new QLineEdit(rootWidget);
    m_exportPathEdit->setPlaceholderText(tr("Select the target mod folder"));
    m_exportBrowseBtn = new QPushButton(tr("Browse..."), rootWidget);
    m_exportBtn = new QPushButton(tr("Export Mod"), rootWidget);
    exportRow->addWidget(exportLabel);
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
    auto* shapeCombo = m_materialPreview->shapeCombo();
    shapeCombo->setParent(previewContainer);
    shapeCombo->setVisible(false);
    previewLayout->addWidget(shapeCombo);

    // 3D control bar: light intensity slider + light color button
    m_3dControlBar = new QWidget(previewContainer);
    auto* controlLayout = new QHBoxLayout(m_3dControlBar);
    controlLayout->setContentsMargins(4, 2, 4, 2);
    controlLayout->setSpacing(6);

    controlLayout->addWidget(new QLabel(tr("Light:"), m_3dControlBar));

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

    m_3dControlBar->setVisible(false);
    previewLayout->addWidget(m_3dControlBar);

    connect(m_lightIntensitySlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                float intensity = static_cast<float>(value) / 10.0f;
                m_lightIntensityLabel->setText(QString::number(intensity, 'f', 1));
                m_materialPreview->setLightIntensity(intensity);
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

    iblLayout->addWidget(new QLabel(tr("HDRI:"), iblRow));

    m_hdriCombo = new QComboBox(iblRow);
    m_hdriCombo->addItem(tr("(None)"), QString());
    m_hdriCombo->setMinimumWidth(150);
    iblLayout->addWidget(m_hdriCombo);

    iblLayout->addWidget(new QLabel(tr("IBL:"), iblRow));

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
            auto files = IBLProcessor::listHDRIs(dir);
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
                    m_materialPreview->setIBLIntensity(0);
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
            [this, iblRow]()
            {
                m_preview3DMode = false;
                m_preview2DBtn->setChecked(true);
                m_preview3DBtn->setChecked(false);
                m_materialPreview->shapeCombo()->setVisible(false);
                m_3dControlBar->setVisible(false);
                iblRow->setVisible(false);
                refreshPreview();
            });
    connect(m_preview3DBtn, &QToolButton::clicked, this,
            [this, iblRow]()
            {
                m_preview3DMode = true;
                m_preview2DBtn->setChecked(false);
                m_preview3DBtn->setChecked(true);
                m_materialPreview->shapeCombo()->setVisible(true);
                m_3dControlBar->setVisible(true);
                iblRow->setVisible(true);
                refresh3DPreview();
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
    connect(m_featurePanel, &FeatureTogglePanel::featuresChanged, this, &MainWindow::onFeaturesChanged);
    connect(m_paramPanel, &ParameterPanel::parametersChanged, this, &MainWindow::onParametersChanged);
    connect(m_slotEditor, &SlotEditorWidget::landscapeEdidsChanged, this, &MainWindow::onLandscapeEdidsChanged);
}

void MainWindow::setupStatusBar()
{
    statusBar()->showMessage(tr("Ready"));
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
    auto path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(),
                                             tr("TruePBR Project (*.tpbr);;All Files (*)"));
    if (path.isEmpty())
        return;

    m_project = Project::load(path.toStdString());
    m_projectFilePath = path.toStdString();
    m_currentSetIndex = -1;
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
        suggestedPath = defaultProjectName(m_project) + QStringLiteral(".tpbr");
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

    if (ModExporter::exportMod(m_project))
    {
        QMessageBox::information(this, tr("Export"), tr("Export successful!"));
    }
    else
    {
        QMessageBox::warning(this, tr("Export"), tr("Export failed. Check log for details."));
    }
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

void MainWindow::onFeaturesChanged(const PBRFeatureFlags& flags)
{
    if (m_currentSetIndex < 0)
        return;
    m_project.textureSets[m_currentSetIndex].features = flags;
    m_slotEditor->updateSlots(flags);
    m_paramPanel->setParameters(m_project.textureSets[m_currentSetIndex].params, flags);
}

void MainWindow::onParametersChanged(const PBRParameters& params)
{
    if (m_currentSetIndex < 0)
        return;
    m_project.textureSets[m_currentSetIndex].params = params;
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

    if (m_previewStack != nullptr)
    {
        m_previewStack->setCurrentWidget(m_materialPreview);
    }

    statusBar()->showMessage(tr("3D Preview: %1").arg(QString::fromStdString(ts.name)));
}

} // namespace tpbr
