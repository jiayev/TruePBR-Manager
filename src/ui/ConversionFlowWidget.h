#pragma once

#include "core/VanillaConverter.h"

#include <QWidget>

#include <map>
#include <vector>

namespace tpbr
{

/// Read-only QPainter widget that visualises the vanilla-to-PBR
/// conversion pipeline as a three-column flow diagram.
///
/// Left column  — vanilla input nodes (8 types)
/// Centre column — processing / transformation nodes
/// Right column  — PBR output nodes
///
/// Nodes have three visual states; arrows between them activate
/// automatically based on the source and target node states.
class ConversionFlowWidget : public QWidget
{
    Q_OBJECT

  public:
    /// Visual state for each node in the diagram.
    enum class NodeState
    {
        Inactive, ///< Grey — not involved in current conversion
        Active,   ///< Highlighted — texture provided / output generated
        Error,    ///< Red — conversion error for this slot
    };

    explicit ConversionFlowWidget(QWidget* parent = nullptr);

    // ── Public API ─────────────────────────────────────────

    /// Toggle a vanilla input node on/off and propagate to
    /// connected processing & output nodes.
    void setInputActive(VanillaTextureType type, bool active);

    /// Set the state of any node by its string identifier.
    void setNodeState(const QString& nodeId, NodeState state);

    /// Reset every node to Inactive.
    void clear();

    /// Recommended minimum size so layout text remains legible.
    QSize minimumSizeHint() const override;

    /// Preferred size at default DPI.
    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    // ── Data model ─────────────────────────────────────────

    /// A single node in the flow diagram.
    struct Node
    {
        QString id;    ///< Unique identifier (e.g. "input_diffuse", "proc_shininess", "out_albedo")
        QString label; ///< Displayed text (translatable)
        NodeState state = NodeState::Inactive;
    };

    /// A directed connection between two nodes.
    struct Arrow
    {
        QString sourceId;
        QString targetId;
    };

    // ── Topology ───────────────────────────────────────────

    /// Ordered lists per column — paint order matches insertion order.
    std::vector<Node> inputNodes_;
    std::vector<Node> processingNodes_;
    std::vector<Node> outputNodes_;

    /// Arrows that connect nodes across columns.
    std::vector<Arrow> inputToProcessing_;
    std::vector<Arrow> processingToOutput_;

    /// Fast look-up by node id → pointer into one of the three vectors.
    std::map<QString, Node*> nodeIndex_;

    /// Maps VanillaTextureType → input-node id for setInputActive().
    std::map<VanillaTextureType, QString> typeToInputId_;

    /// Maps input-node id → list of processing-node ids it feeds.
    std::map<QString, std::vector<QString>> inputToProcessingIds_;

    /// Maps processing-node id → list of output-node ids it feeds.
    std::map<QString, std::vector<QString>> processingToOutputIds_;

    // ── Layout constants (in logical / design coordinates) ─

    static constexpr int kDesignWidth = 960;
    static constexpr int kDesignHeight = 540;

    static constexpr int kNodeWidth = 180;
    static constexpr int kNodeHeight = 36;
    static constexpr int kNodeRadius = 6;

    static constexpr int kArrowHeadSize = 8;

    // ── Helpers ────────────────────────────────────────────

    void buildTopology();
    void rebuildIndex();
    void retranslateUi();

    /// Propagate Active/Inactive from input nodes to processing
    /// and output nodes based on arrow connectivity.
    void propagateStates();

    // ── Drawing helpers (operate on the transformed QPainter) ─

    void drawNode(QPainter& painter, const QRectF& rect, const QString& label, NodeState state) const;
    void drawArrow(QPainter& painter, const QPointF& from, const QPointF& to, bool active) const;

    /// Compute the bounding rect of a node given its column and row.
    static QRectF nodeRect(int column, int row);

    /// Determine arrow colour/state from source + target.
    static bool isArrowActive(NodeState source, NodeState target);
};

} // namespace tpbr
