#pragma once

#include "core/PBRTextureSet.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include <functional>
#include <map>
#include <variant>

namespace tpbr {

// ─── Drop Zone ─────────────────────────────────────────────

/// A label that accepts drag-and-drop of image files, shows a thumbnail
/// and filename, and emits a signal when a file is dropped.
class DropZoneLabel : public QLabel {
    Q_OBJECT

public:
    explicit DropZoneLabel(QWidget* parent = nullptr);

    /// Set the displayed image from a file path. Shows thumbnail + filename.
    void setFile(const std::filesystem::path& path);

    /// Clear the display back to "(empty)" / "(drop here)"
    void clear();

signals:
    void fileDropped(const QString& filePath);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap m_thumbnail;
    QString m_filename;
    bool    m_dragHover = false;

    static constexpr int ThumbnailSize = 48;
};

// ─── Slot Editor Widget ────────────────────────────────────

/// Per-slot editor: shows match path, slot rows with thumbnails
/// and drag-and-drop, plus RMAOS channel import.
class SlotEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit SlotEditorWidget(QWidget* parent = nullptr);

    void updateSlots(const PBRFeatureFlags& features);
    void setTextureSet(const PBRTextureSet& ts);

signals:
    void importRequested(PBRTextureSlot slot);
    void importChannelRequested(ChannelMap channel);
    void matchTextureChanged(const QString& newPath);

    /// Emitted when a file is dropped onto a texture slot
    void fileDroppedOnSlot(PBRTextureSlot slot, const QString& filePath);
    /// Emitted when a file is dropped onto a channel row
    void fileDroppedOnChannel(ChannelMap channel, const QString& filePath);

private:
    void setupUI();
    void addSlotRow(PBRTextureSlot slot, const QString& label, bool visible);
    void addChannelRow(ChannelMap channel, const QString& label);

    QLineEdit* m_matchTextureEdit = nullptr;

    struct SlotRow {
        QLabel*        labelWidget  = nullptr;
        DropZoneLabel* dropZone     = nullptr;
        QPushButton*   importButton = nullptr;
        QWidget*       container    = nullptr;
    };

    struct ChannelRowData {
        QLabel*        labelWidget  = nullptr;
        DropZoneLabel* dropZone     = nullptr;
        QPushButton*   importButton = nullptr;
        QWidget*       container    = nullptr;
    };

    std::map<PBRTextureSlot, SlotRow>      m_slotRows;
    std::map<ChannelMap, ChannelRowData>    m_channelRows;
    QWidget* m_channelSection = nullptr;
};

} // namespace tpbr
