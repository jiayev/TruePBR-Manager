#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

namespace tpbr
{

/// Dialog for selecting the export target mod folder.
class ExportDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit ExportDialog(QWidget* parent = nullptr);

    QString modFolderPath() const;

  private:
    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};

} // namespace tpbr
