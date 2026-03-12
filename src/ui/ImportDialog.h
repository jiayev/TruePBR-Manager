#pragma once

#include "core/PBRTextureSet.h"

#include <QDialog>
#include <filesystem>

namespace tpbr {

/// Dialog for importing texture files into a slot.
class ImportDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportDialog(PBRTextureSlot slot, QWidget* parent = nullptr);

    std::filesystem::path selectedFile() const;

private:
    PBRTextureSlot m_slot;
    std::filesystem::path m_selectedPath;
};

} // namespace tpbr
