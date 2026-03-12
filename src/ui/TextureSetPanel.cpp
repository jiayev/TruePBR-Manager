#include "TextureSetPanel.h"

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace tpbr {

TextureSetPanel::TextureSetPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void TextureSetPanel::setupUI()
{
    auto* layout = new QVBoxLayout(this);

    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);

    auto* buttonLayout = new QHBoxLayout();
    m_addButton    = new QPushButton(tr("Add"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_removeButton);
    layout->addLayout(buttonLayout);

    connect(m_listWidget, &QListWidget::currentRowChanged, this, &TextureSetPanel::textureSetSelected);
    connect(m_addButton,  &QPushButton::clicked, this, &TextureSetPanel::addRequested);
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        int idx = m_listWidget->currentRow();
        if (idx >= 0)
            emit removeRequested(idx);
    });
}

void TextureSetPanel::setTextureSets(const std::vector<PBRTextureSet>& sets)
{
    m_listWidget->clear();
    for (const auto& ts : sets) {
        m_listWidget->addItem(QString::fromStdString(ts.name));
    }

    m_removeButton->setEnabled(!sets.empty());
}

void TextureSetPanel::setCurrentIndex(int index)
{
    const QSignalBlocker blocker(m_listWidget);
    m_listWidget->setCurrentRow(index);
}

int TextureSetPanel::currentIndex() const
{
    return m_listWidget->currentRow();
}

} // namespace tpbr
