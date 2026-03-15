#include "TextureSetPanel.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace tpbr
{

TextureSetPanel::TextureSetPanel(QWidget* parent) : QWidget(parent)
{
    setupUI();
}

void TextureSetPanel::setupUI()
{
    auto* layout = new QVBoxLayout(this);

    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);

    auto* buttonLayout = new QHBoxLayout();
    m_addButton = new QPushButton(tr("Add"), this);
    m_renameButton = new QPushButton(tr("Rename"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_renameButton);
    buttonLayout->addWidget(m_removeButton);
    layout->addLayout(buttonLayout);

    connect(m_listWidget, &QListWidget::currentRowChanged, this, &TextureSetPanel::textureSetSelected);
    connect(m_addButton, &QPushButton::clicked, this, &TextureSetPanel::addRequested);
    connect(m_renameButton, &QPushButton::clicked, this,
            [this]()
            {
                int idx = m_listWidget->currentRow();
                if (idx >= 0)
                {
                    emit renameRequested(idx);
                }
            });
    connect(m_removeButton, &QPushButton::clicked, this,
            [this]()
            {
                int idx = m_listWidget->currentRow();
                if (idx >= 0)
                    emit removeRequested(idx);
            });
}

void TextureSetPanel::setTextureSets(const std::vector<PBRTextureSet>& sets)
{
    const QSignalBlocker blocker(m_listWidget);
    m_listWidget->clear();
    for (const auto& ts : sets)
    {
        QString label = QString::fromStdString(ts.name);
        if (!ts.landscapeEdids.empty())
        {
            label += tr(" [+Landscape]");
        }
        m_listWidget->addItem(label);
    }

    const bool hasSets = !sets.empty();
    m_renameButton->setEnabled(hasSets);
    m_removeButton->setEnabled(hasSets);
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

void TextureSetPanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void TextureSetPanel::retranslateUi()
{
    m_addButton->setText(tr("Add"));
    m_renameButton->setText(tr("Rename"));
    m_removeButton->setText(tr("Remove"));
}

} // namespace tpbr
