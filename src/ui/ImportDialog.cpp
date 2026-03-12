#include "ImportDialog.h"

#include "core/TextureImporter.h"

#include <QFileDialog>

namespace tpbr
{

ImportDialog::ImportDialog(PBRTextureSlot slot, QWidget* parent) : QDialog(parent), m_slot(slot)
{
    setWindowTitle(tr("Import %1").arg(slotDisplayName(slot)));

    auto path = QFileDialog::getOpenFileName(this, tr("Import %1").arg(slotDisplayName(slot)), QString(),
                                             TextureImporter::fileFilter());

    if (!path.isEmpty())
    {
        m_selectedPath = path.toStdString();
        accept();
    }
    else
    {
        reject();
    }
}

std::filesystem::path ImportDialog::selectedFile() const
{
    return m_selectedPath;
}

} // namespace tpbr
