#pragma once

#include "core/PBRTextureSet.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include <functional>
#include <map>

namespace tpbr {

/// Per-slot editor: shows slot name, file path, thumbnail, and import button.
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

private:
    void setupUI();
    void addSlotRow(PBRTextureSlot slot, const QString& label, bool visible);

    struct SlotRow {
        QLabel*      labelWidget   = nullptr;
        QLabel*      pathLabel     = nullptr;
        QPushButton* importButton  = nullptr;
        QWidget*     container     = nullptr;
    };

    std::map<PBRTextureSlot, SlotRow> m_slotRows;
    QWidget* m_channelPackSection = nullptr;
};

} // namespace tpbr
