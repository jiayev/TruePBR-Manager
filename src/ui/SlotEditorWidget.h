#pragma once

#include "core/PBRTextureSet.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QWidget>

#include <functional>
#include <map>
#include <variant>

namespace tpbr
{

// ─── Drop Zone ─────────────────────────────────────────────

/// A label that accepts drag-and-drop of image files, shows a thumbnail
/// and filename, and emits a signal when a file is dropped.
class DropZoneLabel : public QLabel
{
    Q_OBJECT

  public:
    explicit DropZoneLabel(QWidget* parent = nullptr);

    /// Set the displayed image from a file path. Shows thumbnail + filename.
    void setFile(const std::filesystem::path& path, const QString& detailText = QString());

    /// Clear the display back to "(empty)" / "(drop here)"
    void clear();

  signals:
    void clicked();
    void fileDropped(const QString& filePath);

  protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    QPixmap m_thumbnail;
    QString m_filename;
    QString m_detailText;
    bool m_dragHover = false;

    static constexpr int ThumbnailSize = 48;
};

// ─── Slot Editor Widget ────────────────────────────────────

/// Per-slot editor: shows match path, slot rows with thumbnails
/// and drag-and-drop, plus RMAOS channel import.
class SlotEditorWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit SlotEditorWidget(QWidget* parent = nullptr);

    void updateSlots(const PBRFeatureFlags& features);
    void setTextureSet(const PBRTextureSet& ts);

  signals:
    void importRequested(PBRTextureSlot slot);
    void importChannelRequested(ChannelMap channel);
    void rmaosSourceModeChanged(RMAOSSourceMode mode);
    void matchTextureChanged(const QString& newPath);
    void matchTextureModeChanged(TextureMatchMode mode);
    void exportCompressionChanged(PBRTextureSlot slot, DDSCompressionMode mode);

    /// Emitted when a slot's export size changes. {0,0} means use original size.
    void exportSizeChanged(PBRTextureSlot slot, int width, int height);

    /// Emitted when a file is dropped onto a texture slot
    void fileDroppedOnSlot(PBRTextureSlot slot, const QString& filePath);
    /// Emitted when a file is dropped onto a channel row
    void fileDroppedOnChannel(ChannelMap channel, const QString& filePath);

    /// Emitted when a slot's drop zone is clicked (for preview)
    void slotPreviewRequested(PBRTextureSlot slot);

    /// Emitted when a channel's drop zone is clicked (for preview)
    void channelPreviewRequested(ChannelMap channel);

    /// Emitted when a slot texture is cleared (unloaded)
    void slotCleared(PBRTextureSlot slot);

    /// Emitted when a channel map is cleared (unloaded)
    void channelCleared(ChannelMap channel);

    /// Emitted when the landscape EDID list changes
    void landscapeEdidsChanged(const QStringList& edids);

    /// Emitted when the match aliases change
    void matchAliasesChanged(const QStringList& aliases);

    /// Emitted when a slot's path override changes (empty string = disabled)
    void slotPathOverrideChanged(PBRTextureSlot slot, const QString& path);

  private:
    void setupUI();
    void addSlotRow(PBRTextureSlot slot, const QString& label, bool visible);
    void addChannelRow(ChannelMap channel, const QString& label);
    void updateRmaosModeUI(RMAOSSourceMode mode);

    QLineEdit* m_matchTextureEdit = nullptr;
    QComboBox* m_matchModeCombo = nullptr;
    QRadioButton* m_packedRmaosRadio = nullptr;
    QRadioButton* m_splitRmaosRadio = nullptr;

    struct SlotRow
    {
        QLabel* labelWidget = nullptr;
        DropZoneLabel* dropZone = nullptr;
        QPushButton* importButton = nullptr;
        QPushButton* clearButton = nullptr;
        QComboBox* compressionCombo = nullptr;
        QComboBox* exportSizeCombo = nullptr;
        QWidget* container = nullptr;
        QCheckBox* pathOverrideCheckbox = nullptr;
        QLineEdit* pathOverrideEdit = nullptr;
        QWidget* pathOverrideContainer = nullptr;
    };

    struct ChannelRowData
    {
        QLabel* labelWidget = nullptr;
        DropZoneLabel* dropZone = nullptr;
        QPushButton* importButton = nullptr;
        QPushButton* clearButton = nullptr;
        QWidget* container = nullptr;
    };

    std::map<PBRTextureSlot, SlotRow> m_slotRows;
    std::map<ChannelMap, ChannelRowData> m_channelRows;
    QWidget* m_rmaosModeContainer = nullptr;
    QWidget* m_channelSection = nullptr;
    QPlainTextEdit* m_landscapeEdidEdit = nullptr;
    QWidget* m_landscapeSection = nullptr;
    QLabel* m_matchLabel = nullptr;
    QLabel* m_rmaosModeLabel = nullptr;
    QLabel* m_landscapeHintLabel = nullptr;
    QPlainTextEdit* m_aliasEdit = nullptr;
    QWidget* m_aliasSection = nullptr;
    QLabel* m_aliasHintLabel = nullptr;

    void retranslateUi();

  protected:
    void changeEvent(QEvent* event) override;
};

} // namespace tpbr
