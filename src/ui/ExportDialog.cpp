#include "ExportDialog.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace tpbr {

ExportDialog::ExportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export to Mod Folder"));
    setMinimumWidth(450);

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("Select the target mod folder:"), this));

    auto* pathLayout = new QHBoxLayout();
    m_pathEdit  = new QLineEdit(this);
    m_browseBtn = new QPushButton(tr("Browse..."), this);
    pathLayout->addWidget(m_pathEdit, 1);
    pathLayout->addWidget(m_browseBtn);
    layout->addLayout(pathLayout);

    auto* btnLayout = new QHBoxLayout();
    m_exportBtn = new QPushButton(tr("Export"), this);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addStretch();
    btnLayout->addWidget(m_exportBtn);
    btnLayout->addWidget(m_cancelBtn);
    layout->addLayout(btnLayout);

    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        auto dir = QFileDialog::getExistingDirectory(this, tr("Select Mod Folder"));
        if (!dir.isEmpty())
            m_pathEdit->setText(dir);
    });

    connect(m_exportBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

QString ExportDialog::modFolderPath() const
{
    return m_pathEdit->text();
}

} // namespace tpbr
