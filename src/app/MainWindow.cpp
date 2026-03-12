#include "MainWindow.h"

#include "core/JsonExporter.h"
#include "core/ModExporter.h"
#include "core/TextureImporter.h"
#include "ui/FeatureTogglePanel.h"
#include "ui/ParameterPanel.h"
#include "ui/SlotEditorWidget.h"
#include "ui/TexturePreviewWidget.h"
#include "ui/TextureSetPanel.h"

#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"
#include <QFileDialog>
#include <QFileInfo>
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

namespace tpbr {

static const char* channelDisplayName(ChannelMap channel)
{
    switch (channel) {
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

    if (ext == ".dds") {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgbaPixels;
        if (!DDSUtils::loadDDS(path, width, height, rgbaPixels) || rgbaPixels.empty()) {
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
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
    fileMenu->addAction(tr("&New Project"),  this, &MainWindow::onNewProject,  QKeySequence::New);
    fileMenu->addAction(tr("&Open Project"), this, &MainWindow::onOpenProject, QKeySequence::Open);
    fileMenu->addAction(tr("&Save Project"), this, &MainWindow::onSaveProject, QKeySequence::Save);
    fileMenu->addAction(tr("Save Project &As..."), this, &MainWindow::onSaveProjectAs, QKeySequence::SaveAs);
    fileMenu->addAction(tr("Project &Name..."), this, &MainWindow::onRenameProject);
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

    m_slotEditor   = new SlotEditorWidget(this);
    m_featurePanel = new FeatureTogglePanel(this);
    m_paramPanel   = new ParameterPanel(this);

    middleLayout->addWidget(m_slotEditor);
    middleLayout->addWidget(m_featurePanel);
    middleLayout->addWidget(m_paramPanel);
    middleLayout->addStretch();

    middleScroll->setWidget(middleWidget);

    m_editorStack = new QStackedWidget(this);
    m_editorStack->addWidget(middleScroll);
    m_editorPlaceholder = createPlaceholderLabel(
        tr("No Texture Set selected.\n\nUse Add to create one, or select an existing Texture Set from the list on the left."),
        m_editorStack);
    m_editorStack->addWidget(m_editorPlaceholder);
    splitter->addWidget(m_editorStack);

    // Right: Texture Preview
    m_previewWidget = new TexturePreviewWidget(this);
    m_previewStack = new QStackedWidget(this);
    m_previewStack->addWidget(m_previewWidget);
    m_previewPlaceholder = createPlaceholderLabel(
        tr("Preview is unavailable until a Texture Set is selected and at least one texture is imported."),
        m_previewStack);
    m_previewStack->addWidget(m_previewPlaceholder);
    splitter->addWidget(m_previewStack);

    splitter->setStretchFactor(0, 1);  // list
    splitter->setStretchFactor(1, 2);  // editor
    splitter->setStretchFactor(2, 3);  // preview

        rootLayout->addWidget(splitter, 1);
        setCentralWidget(rootWidget);

    // Connections
        connect(m_exportPathEdit, &QLineEdit::editingFinished,
            this, &MainWindow::onExportPathEdited);
        connect(m_exportBrowseBtn, &QPushButton::clicked,
            this, &MainWindow::onBrowseExportFolder);
        connect(m_exportBtn, &QPushButton::clicked,
            this, &MainWindow::onExportMod);
    connect(m_textureSetPanel, &TextureSetPanel::textureSetSelected,
            this, &MainWindow::onTextureSetSelected);
    connect(m_textureSetPanel, &TextureSetPanel::addRequested,
            this, &MainWindow::onAddTextureSet);
        connect(m_textureSetPanel, &TextureSetPanel::renameRequested,
            this, &MainWindow::onRenameTextureSet);
    connect(m_textureSetPanel, &TextureSetPanel::removeRequested,
            this, &MainWindow::onRemoveTextureSet);
    connect(m_slotEditor, &SlotEditorWidget::importRequested,
            this, &MainWindow::onImportTexture);
    connect(m_slotEditor, &SlotEditorWidget::importChannelRequested,
            this, &MainWindow::onImportChannel);
    connect(m_slotEditor, &SlotEditorWidget::fileDroppedOnSlot,
            this, &MainWindow::onDroppedOnSlot);
    connect(m_slotEditor, &SlotEditorWidget::fileDroppedOnChannel,
            this, &MainWindow::onDroppedOnChannel);
        connect(m_slotEditor, &SlotEditorWidget::rmaosSourceModeChanged,
            this, &MainWindow::onRmaosSourceModeChanged);
    connect(m_slotEditor, &SlotEditorWidget::matchTextureChanged,
            this, &MainWindow::onMatchTextureChanged);
        connect(m_slotEditor, &SlotEditorWidget::matchTextureModeChanged,
            this, &MainWindow::onMatchTextureModeChanged);
    connect(m_slotEditor, &SlotEditorWidget::exportCompressionChanged,
            this, &MainWindow::onExportCompressionChanged);
    connect(m_featurePanel, &FeatureTogglePanel::featuresChanged,
            this, &MainWindow::onFeaturesChanged);
    connect(m_paramPanel, &ParameterPanel::parametersChanged,
            this, &MainWindow::onParametersChanged);
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
    auto path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(), tr("TruePBR Project (*.tpbr);;All Files (*)"));
    if (path.isEmpty()) return;

    m_project = Project::load(path.toStdString());
    m_projectFilePath = path.toStdString();
    m_currentSetIndex = -1;
    refreshUI();
    statusBar()->showMessage(tr("Opened: %1").arg(path));
}

void MainWindow::onSaveProject()
{
    if (!m_projectFilePath.empty()) {
        saveProjectToPath(QString::fromStdString(m_projectFilePath.string()));
        return;
    }

    const QString path = promptProjectSavePath();
    if (path.isEmpty()) {
        return;
    }

    saveProjectToPath(path);
}

void MainWindow::onSaveProjectAs()
{
    const QString path = promptProjectSavePath();
    if (path.isEmpty()) {
        return;
    }

    saveProjectToPath(path);
}

void MainWindow::onRenameProject()
{
    bool ok = false;
    const QString currentName = defaultProjectName(m_project);
    const QString newName = QInputDialog::getText(
        this,
        tr("Project Name"),
        tr("Project name:"),
        QLineEdit::Normal,
        currentName,
        &ok).trimmed();

    if (!ok || newName.isEmpty()) {
        return;
    }

    m_project.name = newName.toStdString();
    refreshUI();
    statusBar()->showMessage(tr("Project renamed to: %1").arg(newName));
}

QString MainWindow::promptProjectSavePath() const
{
    QString suggestedPath;
    if (!m_projectFilePath.empty()) {
        suggestedPath = QString::fromStdString(m_projectFilePath.string());
    } else {
        suggestedPath = defaultProjectName(m_project) + QStringLiteral(".tpbr");
    }

    QString path = QFileDialog::getSaveFileName(
        const_cast<MainWindow*>(this),
        tr("Save Project"),
        suggestedPath,
        tr("TruePBR Project (*.tpbr)"));

    if (path.isEmpty()) {
        return {};
    }

    if (!path.endsWith(QStringLiteral(".tpbr"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".tpbr");
    }

    return path;
}

bool MainWindow::saveProjectToPath(const QString& path)
{
    if (isPlaceholderProjectName(m_project)) {
        m_project.name = QFileInfo(path).completeBaseName().toStdString();
    }

    if (!m_project.save(path.toStdString())) {
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

    if (m_project.textureSets.empty()) {
        QMessageBox::information(this, tr("Export"), tr("Add at least one Texture Set before exporting."));
        return;
    }

    if (m_project.outputModFolder.empty()) {
        QMessageBox::warning(this, tr("Export"), tr("Please choose an export folder first."));
        return;
    }

    if (ModExporter::exportMod(m_project)) {
        QMessageBox::information(this, tr("Export"), tr("Export successful!"));
    } else {
        QMessageBox::warning(this, tr("Export"), tr("Export failed. Check log for details."));
    }
}

void MainWindow::onBrowseExportFolder()
{
    const QString currentPath = m_exportPathEdit ? m_exportPathEdit->text() : QString();
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Mod Folder"),
        currentPath);

    if (dir.isEmpty()) {
        return;
    }

    m_project.outputModFolder = dir.toStdString();
    if (m_exportPathEdit) {
        m_exportPathEdit->setText(dir);
    }
    statusBar()->showMessage(tr("Export folder set: %1").arg(dir));
}

void MainWindow::onExportPathEdited()
{
    if (!m_exportPathEdit) {
        return;
    }

    m_project.outputModFolder = m_exportPathEdit->text().trimmed().toStdString();
}

void MainWindow::onTextureSetSelected(int index)
{
    m_currentSetIndex = index;
    if (index >= 0 && index < static_cast<int>(m_project.textureSets.size())) {
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
    if (!ok || name.isEmpty()) return;

    auto match = QInputDialog::getText(this, tr("Vanilla Texture Match"),
        tr("Vanilla albedo path (e.g. architecture\\whiterun\\wrwoodplank01):"),
        QLineEdit::Normal, "", &ok);
    if (!ok || match.isEmpty()) return;

    m_project.addTextureSet(name.toStdString(), match.toStdString());
    m_currentSetIndex = static_cast<int>(m_project.textureSets.size()) - 1;
    refreshUI();
    onTextureSetSelected(m_currentSetIndex);

    statusBar()->showMessage(tr("Added texture set: %1").arg(name));
}

void MainWindow::onRenameTextureSet(int index)
{
    if (index < 0 || index >= static_cast<int>(m_project.textureSets.size())) {
        return;
    }

    bool ok = false;
    const QString currentName = QString::fromStdString(m_project.textureSets[index].name);
    const QString newName = QInputDialog::getText(
        this,
        tr("Rename Texture Set"),
        tr("Texture Set name:"),
        QLineEdit::Normal,
        currentName,
        &ok).trimmed();

    if (!ok || newName.isEmpty() || newName == currentName) {
        return;
    }

    m_project.textureSets[index].name = newName.toStdString();
    refreshUI();
    if (index == m_currentSetIndex) {
        onTextureSetSelected(index);
    }
    statusBar()->showMessage(tr("Renamed texture set to: %1").arg(newName));
}

void MainWindow::onRemoveTextureSet(int index)
{
    if (index < 0 || index >= static_cast<int>(m_project.textureSets.size())) return;

    auto reply = QMessageBox::question(this, tr("Remove"),
        tr("Remove texture set '%1'?").arg(QString::fromStdString(m_project.textureSets[index].name)));

    if (reply == QMessageBox::Yes) {
        m_project.removeTextureSet(index);
        if (m_project.textureSets.empty()) {
            m_currentSetIndex = -1;
        } else if (index >= static_cast<int>(m_project.textureSets.size())) {
            m_currentSetIndex = static_cast<int>(m_project.textureSets.size()) - 1;
        } else {
            m_currentSetIndex = index;
        }
        refreshUI();
        if (m_currentSetIndex >= 0) {
            onTextureSetSelected(m_currentSetIndex);
        } else {
            updateEditorState();
            refreshPreview();
        }
    }
}

void MainWindow::onImportTexture(PBRTextureSlot slot)
{
    if (m_currentSetIndex < 0) return;

    auto path = QFileDialog::getOpenFileName(this,
        tr("Import %1").arg(slotDisplayName(slot)),
        QString(),
        TextureImporter::fileFilter());

    if (path.isEmpty()) return;

    auto entry = TextureImporter::importTexture(path.toStdString(), slot);
    if (slot == PBRTextureSlot::RMAOS) {
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
    if (m_currentSetIndex < 0) return;

    const char* name = channelDisplayName(channel);

    auto path = QFileDialog::getOpenFileName(this,
        tr("Import %1 Channel").arg(name),
        QString(),
        TextureImporter::fileFilter());

    if (path.isEmpty()) return;

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

void MainWindow::onDroppedOnSlot(PBRTextureSlot slot, const QString& filePath)
{
    if (m_currentSetIndex < 0) return;

    auto entry = TextureImporter::importTexture(filePath.toStdString(), slot);
    if (slot == PBRTextureSlot::RMAOS) {
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
    if (m_currentSetIndex < 0) return;

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

void MainWindow::onRmaosSourceModeChanged(RMAOSSourceMode mode)
{
    if (m_currentSetIndex < 0) return;

    m_project.textureSets[m_currentSetIndex].rmaosSourceMode = mode;
    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
}

void MainWindow::onMatchTextureChanged(const QString& newPath)
{
    if (m_currentSetIndex < 0) return;
    m_project.textureSets[m_currentSetIndex].matchTexture = newPath.toStdString();
    spdlog::debug("Match texture updated: {}", newPath.toStdString());
}

void MainWindow::onMatchTextureModeChanged(TextureMatchMode mode)
{
    if (m_currentSetIndex < 0) return;
    m_project.textureSets[m_currentSetIndex].matchMode = mode;
    spdlog::debug("Match texture mode updated: {}", textureMatchModeKey(mode));
}

void MainWindow::onExportCompressionChanged(PBRTextureSlot slot, DDSCompressionMode mode)
{
    if (m_currentSetIndex < 0) return;

    m_project.textureSets[m_currentSetIndex].exportCompression[slot] = mode;
    spdlog::debug("Export compression updated: {} -> {}",
                  slotDisplayName(slot), compressionModeKey(mode));
}

void MainWindow::onFeaturesChanged(const PBRFeatureFlags& flags)
{
    if (m_currentSetIndex < 0) return;
    m_project.textureSets[m_currentSetIndex].features = flags;
    m_slotEditor->updateSlots(flags);
    m_paramPanel->setParameters(m_project.textureSets[m_currentSetIndex].params, flags);
}

void MainWindow::onParametersChanged(const PBRParameters& params)
{
    if (m_currentSetIndex < 0) return;
    m_project.textureSets[m_currentSetIndex].params = params;
}

// ─── Refresh ───────────────────────────────────────────────

void MainWindow::refreshUI()
{
    m_textureSetPanel->setTextureSets(m_project.textureSets);
    if (m_currentSetIndex >= 0 && m_currentSetIndex < static_cast<int>(m_project.textureSets.size())) {
        m_textureSetPanel->setCurrentIndex(m_currentSetIndex);
    } else {
        m_textureSetPanel->setCurrentIndex(-1);
    }
    setWindowTitle(tr("TruePBR Manager - %1").arg(defaultProjectName(m_project)));

    if (m_exportPathEdit) {
        const QString exportPath = QString::fromStdString(m_project.outputModFolder.string());
        if (m_exportPathEdit->text() != exportPath) {
            m_exportPathEdit->setText(exportPath);
        }
    }

    if (m_exportBtn) {
        m_exportBtn->setEnabled(!m_project.textureSets.empty());
    }

    updateEditorState();

    if (m_currentSetIndex < 0 || m_currentSetIndex >= static_cast<int>(m_project.textureSets.size())) {
        m_previewWidget->clear();
        if (m_previewStack != nullptr) {
            m_previewStack->setCurrentWidget(m_previewPlaceholder);
        }
    }
}

void MainWindow::updateEditorState()
{
    const bool hasSelection = m_currentSetIndex >= 0
        && m_currentSetIndex < static_cast<int>(m_project.textureSets.size());

    if (m_editorStack != nullptr) {
        if (hasSelection) {
            m_editorStack->setCurrentIndex(0);
        } else {
            const bool hasAnyTextureSet = !m_project.textureSets.empty();
            m_editorPlaceholder->setText(hasAnyTextureSet
                ? tr("Select a Texture Set from the list on the left to edit its textures, features, and parameters.")
                : tr("No Texture Sets yet.\n\nUse Add on the left to create your first Texture Set."));
            m_editorStack->setCurrentWidget(m_editorPlaceholder);
        }
    }

    if (!hasSelection && m_previewStack != nullptr) {
        const bool hasAnyTextureSet = !m_project.textureSets.empty();
        m_previewPlaceholder->setText(hasAnyTextureSet
            ? tr("Select a Texture Set to see its texture preview.")
            : tr("Preview will appear here after you create and select a Texture Set."));
        m_previewStack->setCurrentWidget(m_previewPlaceholder);
    }
}

void MainWindow::refreshPreview()
{
    if (m_currentSetIndex < 0 || m_currentSetIndex >= static_cast<int>(m_project.textureSets.size())) {
        m_previewWidget->clear();
        if (m_previewStack != nullptr) {
            m_previewStack->setCurrentWidget(m_previewPlaceholder);
        }
        return;
    }

    const auto& textureSet = m_project.textureSets[m_currentSetIndex];

    const auto tryShowTexture = [this](PBRTextureSlot slot, const PBRTextureSet& set) {
        auto it = set.textures.find(slot);
        if (it == set.textures.end() || it->second.sourcePath.empty()) {
            return false;
        }

        const QImage image = loadPreviewImage(it->second.sourcePath);
        if (image.isNull()) {
            return false;
        }

        m_previewWidget->setImage(image);
        if (m_previewStack != nullptr) {
            m_previewStack->setCurrentWidget(m_previewWidget);
        }
        return true;
    };

    if (tryShowTexture(PBRTextureSlot::Diffuse, textureSet)) {
        return;
    }

    if (tryShowTexture(PBRTextureSlot::Normal, textureSet)) {
        return;
    }

    m_previewWidget->clear();
    if (m_previewStack != nullptr) {
        m_previewPlaceholder->setText(tr("No previewable texture has been imported for the selected Texture Set yet."));
        m_previewStack->setCurrentWidget(m_previewPlaceholder);
    }

    m_previewWidget->clear();
}

} // namespace tpbr
