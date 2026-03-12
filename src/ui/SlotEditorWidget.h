#pragma once

#include "core/PBRTextureSet.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include <map>

namespace tpbr {

/// Per-slot editor: shows match path, slot rows, and RMAOS channel import.
class SlotEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit SlotEditorWidget(QWidget* parent = nullptr);

    /// Set the list of visible slots based on feature flags
    void updateSlots(const PBRFeatureFlags& features);

    /// Set data for display
    void setTextureSet(const PBRTextureSet& ts);

signals:
    void importRequested(PBRTextureSlot slot);
    void importChannelRequested(ChannelMap channel);
    void matchTextureChanged(const QString& newPath);

private:
    void setupUI();
    void addSlotRow(PBRTextureSlot slot, const QString& label, bool visible);
    void addChannelRow(ChannelMap channel, const QString& label);

    // Match texture path editor
    QLineEdit* m_matchTextureEdit = nullptr;

    struct SlotRow {
        QLabel*      labelWidget   = nullptr;
        QLabel*      pathLabel     = nullptr;
        QPushButton* importButton  = nullptr;
        QWidget*     container     = nullptr;
    };

    struct ChannelRow {
        QLabel*      labelWidget   = nullptr;
        QLabel*      pathLabel     = nullptr;
        QPushButton* importButton  = nullptr;
        QWidget*     container     = nullptr;
    };

    std::map<PBRTextureSlot, SlotRow>  m_slotRows;
    std::map<ChannelMap, ChannelRow>   m_channelRows;
    QWidget* m_channelSection = nullptr;  // Container for all channel rows
};

} // namespace tpbr
