#pragma once

#include "binaryninjaapi.h"
#include "flowgraphwidget.h"

#include <QtWidgets/QSplitter>
#include <QtWidgets/QWidget>

// Side-by-side control-flow-graph diff: two FlowGraphWidgets, each rendering one
// function's CFG with its basic blocks colored by match status (identical =
// none, changed = orange, added = green, removed = red). Used as the "Graph"
// mode of BifrostDiffView.
class BifrostCfgDiffView : public QWidget
{
    Q_OBJECT

    QSplitter*       m_split      = nullptr;
    FlowGraphWidget* m_leftGraph  = nullptr;
    FlowGraphWidget* m_rightGraph = nullptr;

    BinaryNinja::Ref<BinaryNinja::BinaryView> m_leftBv;
    BinaryNinja::Ref<BinaryNinja::BinaryView> m_rightBv;

public:
    BifrostCfgDiffView(QWidget* parent,
                       BinaryNinja::Ref<BinaryNinja::BinaryView> leftBv,
                       BinaryNinja::Ref<BinaryNinja::BinaryView> rightBv);

    // Render the CFGs for a matched function pair. Either function may be null
    // (added/removed), in which case that side shows an empty graph.
    void showPair(BinaryNinja::Ref<BinaryNinja::Function> leftFunc,
                  BinaryNinja::Ref<BinaryNinja::Function> rightFunc);
};
