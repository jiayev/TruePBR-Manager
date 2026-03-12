#pragma once

#include "core/PBRTextureSet.h"

#include <QListWidget>
#include <QPushButton>
#include <QWidget>

#include <vector>

namespace tpbr {

/// Panel showing the list of PBR texture sets with add/remove controls.
class TextureSetPanel : public QWidget {
    Q_OBJECT

public:
    explicit TextureSetPanel(QWidget* parent = nullptr);

    void setTextureSets(const std::vector<PBRTextureSet>& sets);
    int  currentIndex() const;

signals:
    void textureSetSelected(int index);
    void addRequested();
    void removeRequested(int index);

private:
    QListWidget* m_listWidget  = nullptr;
    QPushButton* m_addButton   = nullptr;
    QPushButton* m_removeButton = nullptr;

    void setupUI();
};

} // namespace tpbr
