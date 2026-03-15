#pragma once

#include <QDialog>
#include <QLabel>
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
    QLabel* m_descLabel = nullptr;

    void retranslateUi();

  protected:
    void changeEvent(QEvent* event) override;
};

} // namespace tpbr
