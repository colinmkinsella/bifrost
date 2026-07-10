#include "bifrostcfgdiff.h"
#include "bifrostfeatures.h"
#include "bifrostmatch.h"

#include <QtWidgets/QVBoxLayout>

#include <map>

using namespace BinaryNinja;

// ── Helpers ─────────────────────────────────────────────────────────────────

static BNHighlightColor stdColor(BNHighlightStandardColor c)
{
    BNHighlightColor hc{};
    hc.style    = StandardHighlightColor;
    hc.color    = c;
    hc.mixColor = NoHighlightColor;
    hc.mix = hc.r = hc.g = hc.b = 0;
    hc.alpha = 255;
    return hc;
}

// Build a custom FlowGraph for one function, coloring each block by the status
// recorded in colorByAddr (keyed by block start address). Blocks without an
// entry are left un-highlighted.
static Ref<FlowGraph> buildGraph(Ref<Function> func, Ref<BinaryView> bv,
                                 const std::map<uint64_t, BNHighlightStandardColor>& colorByAddr)
{
    Ref<FlowGraph> g = new FlowGraph();
    if (bv)   g->SetView(bv);
    if (func) g->SetFunction(func);
    if (!func) return g;

    Ref<DisassemblySettings> settings = new DisassemblySettings();
    auto blocks = func->GetBasicBlocks();

    std::map<uint64_t, Ref<FlowGraphNode>> nodeByAddr;
    for (auto& bb : blocks)
    {
        Ref<FlowGraphNode> node = new FlowGraphNode(g);
        node->SetBasicBlock(bb);
        node->SetLines(bb->GetDisassemblyText(settings));
        if (auto it = colorByAddr.find(bb->GetStart()); it != colorByAddr.end())
            node->SetHighlight(stdColor(it->second));
        g->AddNode(node);
        nodeByAddr[bb->GetStart()] = node;
    }
    for (auto& bb : blocks)
    {
        Ref<FlowGraphNode> src = nodeByAddr[bb->GetStart()];
        for (auto& e : bb->GetOutgoingEdges())
        {
            if (!e.target) continue;
            if (auto it = nodeByAddr.find(e.target->GetStart()); it != nodeByAddr.end())
                src->AddOutgoingEdge(e.type, it->second);
        }
    }
    return g;
}

// ── BifrostCfgDiffView ──────────────────────────────────────────────────────

BifrostCfgDiffView::BifrostCfgDiffView(QWidget* parent,
                                       Ref<BinaryView> leftBv, Ref<BinaryView> rightBv)
    : QWidget(parent), m_leftBv(leftBv), m_rightBv(rightBv)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->setChildrenCollapsible(false);
    m_leftGraph  = new FlowGraphWidget(m_split, m_leftBv);
    m_rightGraph = new FlowGraphWidget(m_split, m_rightBv);
    m_split->addWidget(m_leftGraph);
    m_split->addWidget(m_rightGraph);
    m_split->setSizes({500, 500});

    layout->addWidget(m_split, 1);
}

void BifrostCfgDiffView::showPair(Ref<Function> leftFunc, Ref<Function> rightFunc)
{
    std::map<uint64_t, BNHighlightStandardColor> lc, rc;

    if (leftFunc && rightFunc && m_leftBv && m_rightBv)
    {
        // Match the blocks to color each side by correspondence.
        bifrost::FeatureExtractor le(m_leftBv), re(m_rightBv);
        bifrost::FuncFeatures lf = le.ExtractFunction(leftFunc);
        bifrost::FuncFeatures rf = re.ExtractFunction(rightFunc);
        bifrost::FuncMatch fm;
        bifrost::Matcher::MatchBlocks(lf, rf, fm);

        for (auto& b : fm.blocks)
        {
            switch (b.status)
            {
            case bifrost::MatchStatus::Identical:
                if (b.leftAddr)  lc[b.leftAddr]  = NoHighlightColor;
                if (b.rightAddr) rc[b.rightAddr] = NoHighlightColor;
                break;
            case bifrost::MatchStatus::Changed:
                if (b.leftAddr)  lc[b.leftAddr]  = OrangeHighlightColor;
                if (b.rightAddr) rc[b.rightAddr] = OrangeHighlightColor;
                break;
            case bifrost::MatchStatus::Removed:
                if (b.leftAddr)  lc[b.leftAddr]  = RedHighlightColor;
                break;
            case bifrost::MatchStatus::Added:
                if (b.rightAddr) rc[b.rightAddr] = GreenHighlightColor;
                break;
            }
        }
    }
    else if (leftFunc)   // removed function — entire CFG in red
    {
        for (auto& bb : leftFunc->GetBasicBlocks()) lc[bb->GetStart()] = RedHighlightColor;
    }
    else if (rightFunc)  // added function — entire CFG in green
    {
        for (auto& bb : rightFunc->GetBasicBlocks()) rc[bb->GetStart()] = GreenHighlightColor;
    }

    m_leftGraph->setGraph(leftFunc   ? buildGraph(leftFunc,  m_leftBv,  lc) : Ref<FlowGraph>(new FlowGraph()));
    m_rightGraph->setGraph(rightFunc ? buildGraph(rightFunc, m_rightBv, rc) : Ref<FlowGraph>(new FlowGraph()));
}
