#include "MainWindow.h"

#include "core/JsonExporter.h"
#include "core/ModExporter.h"
#include "core/TextureImporter.h"
#include "ui/ExportDialog.h"
#include "ui/FeatureTogglePanel.h"
#include "ui/ParameterPanel.h"
#include "ui/SlotEditorWidget.h"
#include "ui/TexturePreviewWidget.h"
#include "ui/TextureSetPanel.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

namespace tpbr {

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
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Export Mod..."), this, &MainWindow::onExportMod);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence::Quit);
}

// ─── Central Widget ────────────────────────────────────────

void MainWindow::setupCentralWidget()
{
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
    splitter->addWidget(middleScroll);

    // Right: Texture Preview
    m_previewWidget = new TexturePreviewWidget(this);
    splitter->addWidget(m_previewWidget);

    splitter->setStretchFactor(0, 1);  // list
    splitter->setStretchFactor(1, 2);  // editor
    splitter->setStretchFactor(2, 3);  // preview

    setCentralWidget(splitter);

    // Connections
    connect(m_textureSetPanel, &TextureSetPanel::textureSetSelected,
            this, &MainWindow::onTextureSetSelected);
    connect(m_textureSetPanel, &TextureSetPanel::addRequested,
            this, &MainWindow::onAddTextureSet);
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
    connect(m_slotEditor, &SlotEditorWidget::matchTextureChanged,
            this, &MainWindow::onMatchTextureChanged);
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
    m_currentSetIndex = -1;
    refreshUI();
    statusBar()->showMessage(tr("New project created"));
}

void MainWindow::onOpenProject()
{
    auto path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(), tr("TruePBR Project (*.tpbr);;All Files (*)"));
    if (path.isEmpty()) return;

    m_project = Project::load(path.toStdString());
    m_currentSetIndex = -1;
    refreshUI();
    statusBar()->showMessage(tr("Opened: %1").arg(path));
}

void MainWindow::onSaveProject()
{
    auto path = QFileDialog::getSaveFileName(this, tr("Save Project"), QString(), tr("TruePBR Project (*.tpbr)"));
    if (path.isEmpty()) return;

    m_project.save(path.toStdString());
    statusBar()->showMessage(tr("Saved: %1").arg(path));
}

void MainWindow::onExportMod()
{
    ExportDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        m_project.outputModFolder = dlg.modFolderPath().toStdString();
        if (ModExporter::exportMod(m_project)) {
            QMessageBox::information(this, tr("Export"), tr("Export successful!"));
        } else {
            QMessageBox::warning(this, tr("Export"), tr("Export failed. Check log for details."));
        }
    }
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
}

void MainWindow::onAddTextureSet()
{
    bool ok = false;
    auto name = QInputDialog::getText(this, tr("New Texture Set"), tr("Name:"), QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;

    auto match = QInputDialog::getText(this, tr("Vanilla Texture Match"),
        tr("Vanilla diffuse path (e.g. architecture\\whiterun\\wrwoodplank01):"),
        QLineEdit::Normal, "", &ok);
    if (!ok || match.isEmpty()) return;

    m_project.addTextureSet(name.toStdString(), match.toStdString());
    refreshUI();

    // Auto-select the new set
    int newIndex = static_cast<int>(m_project.textureSets.size()) - 1;
    m_textureSetPanel->setTextureSets(m_project.textureSets);
    onTextureSetSelected(newIndex);

    statusBar()->showMessage(tr("Added texture set: %1").arg(name));
}

void MainWindow::onRemoveTextureSet(int index)
{
    if (index < 0 || index >= static_cast<int>(m_project.textureSets.size())) return;

    auto reply = QMessageBox::question(this, tr("Remove"),
        tr("Remove texture set '%1'?").arg(QString::fromStdString(m_project.textureSets[index].name)));

    if (reply == QMessageBox::Yes) {
        m_project.removeTextureSet(index);
        m_currentSetIndex = -1;
        refreshUI();
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
    m_project.textureSets[m_currentSetIndex].textures[slot] = entry;

    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
    statusBar()->showMessage(tr("Imported %1: %2 (%3x%4)")
        .arg(slotDisplayName(slot))
        .arg(QString::fromStdString(entry.sourcePath.filename().string()))
        .arg(entry.width)
        .arg(entry.height));
}

void MainWindow::onImportChannel(ChannelMap channel)
{
    if (m_currentSetIndex < 0) return;

    const char* channelNames[] = {"Roughness", "Metallic", "AO", "Specular"};
    const char* name = channelNames[static_cast<int>(channel)];

    auto path = QFileDialog::getOpenFileName(this,
        tr("Import %1 Channel").arg(name),
        QString(),
        TextureImporter::fileFilter());

    if (path.isEmpty()) return;

    m_project.textureSets[m_currentSetIndex].channelMaps[channel] = path.toStdString();

    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
    statusBar()->showMessage(tr("Imported %1 channel: %2").arg(name).arg(path));
}

void MainWindow::onDroppedOnSlot(PBRTextureSlot slot, const QString& filePath)
{
    if (m_currentSetIndex < 0) return;

    auto entry = TextureImporter::importTexture(filePath.toStdString(), slot);
    m_project.textureSets[m_currentSetIndex].textures[slot] = entry;

    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
    statusBar()->showMessage(tr("Dropped %1: %2 (%3x%4)")
        .arg(slotDisplayName(slot))
        .arg(QString::fromStdString(entry.sourcePath.filename().string()))
        .arg(entry.width)
        .arg(entry.height));
}

void MainWindow::onDroppedOnChannel(ChannelMap channel, const QString& filePath)
{
    if (m_currentSetIndex < 0) return;

    const char* channelNames[] = {"Roughness", "Metallic", "AO", "Specular"};
    const char* name = channelNames[static_cast<int>(channel)];

    m_project.textureSets[m_currentSetIndex].channelMaps[channel] = filePath.toStdString();

    m_slotEditor->setTextureSet(m_project.textureSets[m_currentSetIndex]);
    statusBar()->showMessage(tr("Dropped %1 channel: %2").arg(name).arg(filePath));
}

void MainWindow::onMatchTextureChanged(const QString& newPath)
{
    if (m_currentSetIndex < 0) return;
    m_project.textureSets[m_currentSetIndex].matchTexture = newPath.toStdString();
    spdlog::debug("Match texture updated: {}", newPath.toStdString());
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
    setWindowTitle(tr("TruePBR Manager - %1").arg(QString::fromStdString(m_project.name)));
}

} // namespace tpbr
