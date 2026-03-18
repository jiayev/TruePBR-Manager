#include "ConversionFlowWidget.h"

#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tpbr
{

// ─── Layout helpers ────────────────────────────────────────

/// Column X-centres expressed as fractions of kDesignWidth.
static constexpr double kColInputX = 0.12;
static constexpr double kColProcessX = 0.50;
static constexpr double kColOutputX = 0.88;

/// Vertical start and spacing for rows (fraction of kDesignHeight).
static constexpr double kRowStartY = 0.08;
static constexpr double kRowSpacing = 0.115; // ~8 rows fit in 540 design px

QRectF ConversionFlowWidget::nodeRect(int column, int row)
{
    double cx = 0.0;
    switch (column)
    {
    case 0:
        cx = kColInputX * kDesignWidth;
        break;
    case 1:
        cx = kColProcessX * kDesignWidth;
        break;
    case 2:
        cx = kColOutputX * kDesignWidth;
        break;
    default:
        cx = kColInputX * kDesignWidth;
        break;
    }

    const double cy = kRowStartY * kDesignHeight + row * kRowSpacing * kDesignHeight;
    return QRectF(cx - kNodeWidth / 2.0, cy - kNodeHeight / 2.0, kNodeWidth, kNodeHeight);
}

// ─── Topology ──────────────────────────────────────────────

void ConversionFlowWidget::buildTopology()
{
    // ── Input nodes (left column, 8 rows) ──────────────────

    inputNodes_ = {
        {QStringLiteral("in_diffuse"), tr("Diffuse"), NodeState::Inactive},
        {QStringLiteral("in_normal"), tr("Normal"), NodeState::Inactive},
        {QStringLiteral("in_glow"), tr("Glow"), NodeState::Inactive},
        {QStringLiteral("in_parallax"), tr("Parallax"), NodeState::Inactive},
        {QStringLiteral("in_specular"), tr("Specular"), NodeState::Inactive},
        {QStringLiteral("in_backlight"), tr("BackLight"), NodeState::Inactive},
        {QStringLiteral("in_envmask"), tr("EnvMask"), NodeState::Inactive},
        {QStringLiteral("in_cubemap"), tr("Cubemap"), NodeState::Inactive},
    };

    // ── Processing nodes (centre column) ───────────────────

    processingNodes_ = {
        {QStringLiteral("proc_shininess"), tr("Shininess \xe2\x86\x92 Roughness"), NodeState::Inactive},
        {QStringLiteral("proc_normal"), tr("Normal \xe2\x86\x92 Normal"), NodeState::Inactive},
        {QStringLiteral("proc_glow"), tr("Glow \xe2\x86\x92 Emissive"), NodeState::Inactive},
        {QStringLiteral("proc_parallax"), tr("Parallax \xe2\x86\x92 Displacement"), NodeState::Inactive},
        {QStringLiteral("proc_specular"), tr("Specular \xe2\x86\x92 RMAOS.A"), NodeState::Inactive},
        {QStringLiteral("proc_backlight"), tr("BackLight \xe2\x86\x92 Subsurface"), NodeState::Inactive},
        {QStringLiteral("proc_envmask"), tr("EnvMask \xe2\x86\x92 Metallic"), NodeState::Inactive},
        {QStringLiteral("proc_cubemap"), tr("Cubemap \xe2\x86\x92 Tint \xe2\x86\x92 Albedo"), NodeState::Inactive},
    };

    // ── Output nodes (right column, 6 unique outputs) ──────
    // Some rows share an output; we still create one per row
    // so the visual alignment stays 1:1:1.

    outputNodes_ = {
        {QStringLiteral("out_albedo"), tr("Albedo"), NodeState::Inactive},
        {QStringLiteral("out_normal"), tr("Normal"), NodeState::Inactive},
        {QStringLiteral("out_emissive"), tr("Emissive"), NodeState::Inactive},
        {QStringLiteral("out_displacement"), tr("Displacement"), NodeState::Inactive},
        {QStringLiteral("out_rmaos"), tr("RMAOS"), NodeState::Inactive},
        {QStringLiteral("out_subsurface"), tr("Subsurface"), NodeState::Inactive},
        {QStringLiteral("out_metallic"), tr("Metallic"), NodeState::Inactive},
        {QStringLiteral("out_albedo_tint"), tr("Albedo (tinted)"), NodeState::Inactive},
    };

    // ── VanillaTextureType → input id ──────────────────────

    typeToInputId_ = {
        {VanillaTextureType::Diffuse, QStringLiteral("in_diffuse")},
        {VanillaTextureType::Normal, QStringLiteral("in_normal")},
        {VanillaTextureType::Glow, QStringLiteral("in_glow")},
        {VanillaTextureType::Parallax, QStringLiteral("in_parallax")},
        {VanillaTextureType::Specular, QStringLiteral("in_specular")},
        {VanillaTextureType::BackLight, QStringLiteral("in_backlight")},
        {VanillaTextureType::EnvMask, QStringLiteral("in_envmask")},
        {VanillaTextureType::Cubemap, QStringLiteral("in_cubemap")},
    };

    // ── Input → Processing arrows ──────────────────────────

    inputToProcessing_ = {
        {QStringLiteral("in_diffuse"), QStringLiteral("proc_shininess")},
        {QStringLiteral("in_normal"), QStringLiteral("proc_normal")},
        {QStringLiteral("in_glow"), QStringLiteral("proc_glow")},
        {QStringLiteral("in_parallax"), QStringLiteral("proc_parallax")},
        {QStringLiteral("in_specular"), QStringLiteral("proc_specular")},
        {QStringLiteral("in_backlight"), QStringLiteral("proc_backlight")},
        {QStringLiteral("in_envmask"), QStringLiteral("proc_envmask")},
        {QStringLiteral("in_cubemap"), QStringLiteral("proc_cubemap")},
    };

    // ── Processing → Output arrows ─────────────────────────

    processingToOutput_ = {
        {QStringLiteral("proc_shininess"), QStringLiteral("out_albedo")},
        {QStringLiteral("proc_normal"), QStringLiteral("out_normal")},
        {QStringLiteral("proc_glow"), QStringLiteral("out_emissive")},
        {QStringLiteral("proc_parallax"), QStringLiteral("out_displacement")},
        {QStringLiteral("proc_specular"), QStringLiteral("out_rmaos")},
        {QStringLiteral("proc_backlight"), QStringLiteral("out_subsurface")},
        {QStringLiteral("proc_envmask"), QStringLiteral("out_metallic")},
        {QStringLiteral("proc_cubemap"), QStringLiteral("out_albedo_tint")},
    };

    // ── Fast look-up maps ──────────────────────────────────

    inputToProcessingIds_.clear();
    for (const auto& a : inputToProcessing_)
        inputToProcessingIds_[a.sourceId].push_back(a.targetId);

    processingToOutputIds_.clear();
    for (const auto& a : processingToOutput_)
        processingToOutputIds_[a.sourceId].push_back(a.targetId);

    rebuildIndex();
}

void ConversionFlowWidget::rebuildIndex()
{
    nodeIndex_.clear();
    for (auto& n : inputNodes_)
        nodeIndex_[n.id] = &n;
    for (auto& n : processingNodes_)
        nodeIndex_[n.id] = &n;
    for (auto& n : outputNodes_)
        nodeIndex_[n.id] = &n;
}

// ─── Constructor ───────────────────────────────────────────

ConversionFlowWidget::ConversionFlowWidget(QWidget* parent) : QWidget(parent)
{
    buildTopology();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

// ─── Public API ────────────────────────────────────────────

void ConversionFlowWidget::setInputActive(VanillaTextureType type, bool active)
{
    auto it = typeToInputId_.find(type);
    if (it == typeToInputId_.end())
        return;

    auto nodeIt = nodeIndex_.find(it->second);
    if (nodeIt == nodeIndex_.end())
        return;

    nodeIt->second->state = active ? NodeState::Active : NodeState::Inactive;
    propagateStates();
    update();
}

void ConversionFlowWidget::setNodeState(const QString& nodeId, NodeState state)
{
    auto it = nodeIndex_.find(nodeId);
    if (it == nodeIndex_.end())
        return;

    it->second->state = state;
    update();
}

void ConversionFlowWidget::clear()
{
    for (auto& n : inputNodes_)
        n.state = NodeState::Inactive;
    for (auto& n : processingNodes_)
        n.state = NodeState::Inactive;
    for (auto& n : outputNodes_)
        n.state = NodeState::Inactive;
    update();
}

QSize ConversionFlowWidget::minimumSizeHint() const
{
    return {480, 270};
}

QSize ConversionFlowWidget::sizeHint() const
{
    return {kDesignWidth, kDesignHeight};
}

// ─── State propagation ────────────────────────────────────

void ConversionFlowWidget::propagateStates()
{
    // Processing nodes become Active if their feeding input is Active.
    for (auto& proc : processingNodes_)
    {
        bool fed = false;
        for (const auto& a : inputToProcessing_)
        {
            if (a.targetId == proc.id)
            {
                auto srcIt = nodeIndex_.find(a.sourceId);
                if (srcIt != nodeIndex_.end() && srcIt->second->state == NodeState::Active)
                {
                    fed = true;
                    break;
                }
            }
        }
        // Only auto-propagate if not in Error state.
        if (proc.state != NodeState::Error)
            proc.state = fed ? NodeState::Active : NodeState::Inactive;
    }

    // Output nodes become Active if their feeding processing node is Active.
    for (auto& out : outputNodes_)
    {
        bool fed = false;
        for (const auto& a : processingToOutput_)
        {
            if (a.targetId == out.id)
            {
                auto srcIt = nodeIndex_.find(a.sourceId);
                if (srcIt != nodeIndex_.end() && srcIt->second->state == NodeState::Active)
                {
                    fed = true;
                    break;
                }
            }
        }
        if (out.state != NodeState::Error)
            out.state = fed ? NodeState::Active : NodeState::Inactive;
    }
}

// ─── i18n ──────────────────────────────────────────────────

void ConversionFlowWidget::retranslateUi()
{
    // Re-create topology with fresh tr() calls.
    // Preserve current states.
    std::map<QString, NodeState> savedStates;
    for (const auto& [id, ptr] : nodeIndex_)
        savedStates[id] = ptr->state;

    buildTopology();

    // Restore states.
    for (auto& [id, ptr] : nodeIndex_)
    {
        auto it = savedStates.find(id);
        if (it != savedStates.end())
            ptr->state = it->second;
    }

    update();
}

void ConversionFlowWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

// ─── Painting ──────────────────────────────────────────────

bool ConversionFlowWidget::isArrowActive(NodeState source, NodeState target)
{
    // Arrow is active if source is active (or error) and target is active (or error).
    return source != NodeState::Inactive && target != NodeState::Inactive;
}

void ConversionFlowWidget::drawNode(QPainter& painter, const QRectF& rect, const QString& label, NodeState state) const
{
    // Colours
    QColor fillColor;
    QColor borderColor;
    QColor textColor;

    switch (state)
    {
    case NodeState::Active:
        fillColor = palette().highlight().color();
        borderColor = fillColor.darker(130);
        textColor = palette().highlightedText().color();
        break;
    case NodeState::Error:
        fillColor = QColor(0xFF, 0x00, 0x00);
        borderColor = QColor(0xCC, 0x00, 0x00);
        textColor = Qt::white;
        break;
    case NodeState::Inactive:
    default:
        fillColor = QColor(0x80, 0x80, 0x80);
        borderColor = QColor(0x60, 0x60, 0x60);
        textColor = QColor(0xE0, 0xE0, 0xE0);
        break;
    }

    painter.save();

    // Fill
    painter.setBrush(QBrush(fillColor));
    painter.setPen(QPen(borderColor, 2.0));
    painter.drawRoundedRect(rect, kNodeRadius, kNodeRadius);

    // Label
    QFont font = painter.font();
    font.setPixelSize(12);
    painter.setFont(font);
    painter.setPen(textColor);
    painter.drawText(rect, Qt::AlignCenter, label);

    painter.restore();
}

void ConversionFlowWidget::drawArrow(QPainter& painter, const QPointF& from, const QPointF& to, bool active) const
{
    painter.save();

    const QColor color = active ? palette().highlight().color() : QColor(0x60, 0x60, 0x60);
    painter.setPen(QPen(color, 2.0));
    painter.setBrush(Qt::NoBrush);

    // Draw the line
    painter.drawLine(from, to);

    // Arrowhead
    const double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
    const double headSize = kArrowHeadSize;

    const QPointF p1(to.x() - headSize * std::cos(angle - M_PI / 6.0),
                     to.y() - headSize * std::sin(angle - M_PI / 6.0));
    const QPointF p2(to.x() - headSize * std::cos(angle + M_PI / 6.0),
                     to.y() - headSize * std::sin(angle + M_PI / 6.0));

    painter.setBrush(QBrush(color));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(QPolygonF({to, p1, p2}));

    painter.restore();
}

void ConversionFlowWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // ── Scale to fit widget while preserving aspect ratio ───

    const double designAspect = static_cast<double>(kDesignWidth) / kDesignHeight;
    const double widgetAspect = static_cast<double>(width()) / height();

    double scaleFactor = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;

    if (widgetAspect > designAspect)
    {
        // Widget is wider → fit to height
        scaleFactor = static_cast<double>(height()) / kDesignHeight;
        offsetX = (width() - kDesignWidth * scaleFactor) / 2.0;
    }
    else
    {
        // Widget is taller → fit to width
        scaleFactor = static_cast<double>(width()) / kDesignWidth;
        offsetY = (height() - kDesignHeight * scaleFactor) / 2.0;
    }

    painter.translate(offsetX, offsetY);
    painter.scale(scaleFactor, scaleFactor);

    // ── Draw arrows first (behind nodes) ───────────────────

    auto drawArrows = [&](const std::vector<Arrow>& arrows, int srcCol, int dstCol, const std::vector<Node>& srcNodes,
                          const std::vector<Node>& dstNodes)
    {
        for (const auto& arrow : arrows)
        {
            // Find source row
            int srcRow = -1;
            const Node* srcNode = nullptr;
            for (int i = 0; i < static_cast<int>(srcNodes.size()); ++i)
            {
                if (srcNodes[i].id == arrow.sourceId)
                {
                    srcRow = i;
                    srcNode = &srcNodes[i];
                    break;
                }
            }

            // Find dest row
            int dstRow = -1;
            const Node* dstNode = nullptr;
            for (int i = 0; i < static_cast<int>(dstNodes.size()); ++i)
            {
                if (dstNodes[i].id == arrow.targetId)
                {
                    dstRow = i;
                    dstNode = &dstNodes[i];
                    break;
                }
            }

            if (srcRow < 0 || dstRow < 0 || !srcNode || !dstNode)
                continue;

            const QRectF srcRect = nodeRect(srcCol, srcRow);
            const QRectF dstRect = nodeRect(dstCol, dstRow);

            const QPointF from(srcRect.right(), srcRect.center().y());
            const QPointF to(dstRect.left(), dstRect.center().y());

            const bool active = isArrowActive(srcNode->state, dstNode->state);
            drawArrow(painter, from, to, active);
        }
    };

    drawArrows(inputToProcessing_, 0, 1, inputNodes_, processingNodes_);
    drawArrows(processingToOutput_, 1, 2, processingNodes_, outputNodes_);

    // ── Draw nodes ─────────────────────────────────────────

    auto drawNodes = [&](const std::vector<Node>& nodes, int column)
    {
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
        {
            const auto& node = nodes[i];
            const QRectF rect = nodeRect(column, i);
            drawNode(painter, rect, node.label, node.state);
        }
    };

    drawNodes(inputNodes_, 0);
    drawNodes(processingNodes_, 1);
    drawNodes(outputNodes_, 2);
}

} // namespace tpbr
