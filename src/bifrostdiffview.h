#pragma once

#include "bifroststate.h"
#include "binaryninjaapi.h"
#include "pane.h"
#include "viewframe.h"
#include "filecontext.h"

#include <QtWidgets/QPushButton>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QWidget>

// BifrostDiffView shows two full ViewFrames side by side (each with BN's
// native file-type / view-type / IL-type combo toolbar).  The diff function
// list is displayed in the BifrostSidebar (RightBottom) rather than inline.
// A Linear/Graph toggle switches both panes together so the diff can be walked
// as a side-by-side control-flow-graph comparison (block highlights render in
// the graph view too).
class BifrostDiffView : public QWidget
{
    Q_OBJECT

    QSplitter*   m_frameSplit = nullptr;
    QPushButton* m_graphToggle = nullptr;
    bool         m_graphMode = false;

    ViewFrame*       m_leftFrame   = nullptr;
    ViewFrame*       m_rightFrame  = nullptr;
    SplitPaneWidget* m_leftWidget  = nullptr;
    SplitPaneWidget* m_rightWidget = nullptr;

    BinaryNinja::Ref<BinaryNinja::BinaryView> m_leftBv;
    BinaryNinja::Ref<BinaryNinja::BinaryView> m_rightBv;
    QString m_leftBvName;
    QString m_rightBvName;

    BinaryNinja::Ref<BinaryNinja::Function> m_prevLeftFunc;
    BinaryNinja::Ref<BinaryNinja::Function> m_prevRightFunc;

    // Instruction addresses currently highlighted, so they can be cleared on
    // the next navigation without scanning the whole function.
    std::vector<uint64_t> m_leftInstrHi;
    std::vector<uint64_t> m_rightInstrHi;

    static std::string blockSignature(BinaryNinja::Ref<BinaryNinja::BasicBlock> block);
    static QString     bvDisplayName(BinaryNinja::Ref<BinaryNinja::BinaryView> bv);
    BinaryNinja::Ref<BinaryNinja::BinaryView> findBvByName(const QString& name) const;

    static FileContext* fileContextForBv(BinaryNinja::Ref<BinaryNinja::BinaryView> bv);

    SplitPaneWidget* makeSplitPane(BinaryNinja::Ref<BinaryNinja::BinaryView> bv,
                                   ViewFrame*& frameOut);
    void initPanes();
    // Re-resolve the binaries and rebuild both panes (used by the Reload button
    // when the binaries are opened after the diff view).
    void reloadPanes();

    // Switch both panes between the Linear and Graph view types.
    void setPaneViewType(bool graph);

public:
    explicit BifrostDiffView(QWidget* parent,
                             BinaryNinja::Ref<BinaryNinja::Metadata> diffData,
                             const QString& diffName);
    virtual ~BifrostDiffView() override;

    // Called by the sidebar when the user clicks a diff entry.
    void navigateToEntry(uint64_t leftAddr, uint64_t rightAddr, const QString& status);

    static bool isSemanticToken(BNInstructionTextTokenType t);
    void clearBlockHighlights(BinaryNinja::Ref<BinaryNinja::Function> func);
    void applyBlockHighlights(BinaryNinja::Ref<BinaryNinja::Function> lf,
                               BinaryNinja::Ref<BinaryNinja::Function> rf);

    // Clear all block and instruction highlights recorded from the previous
    // navigation.
    void clearHighlights();
    // Highlight differing instructions within a matched-but-changed block pair.
    void highlightInstructionDiffs(BinaryNinja::Ref<BinaryNinja::Function> lf,
                                   BinaryNinja::Ref<BinaryNinja::Function> rf,
                                   BinaryNinja::Ref<BinaryNinja::BasicBlock> lb,
                                   BinaryNinja::Ref<BinaryNinja::BasicBlock> rb);
};
