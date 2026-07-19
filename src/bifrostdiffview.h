#pragma once

#include "bifroststate.h"
#include "binaryninjaapi.h"
#include "pane.h"
#include "viewframe.h"
#include "filecontext.h"

#include <QtWidgets/QSplitter>
#include <QtWidgets/QWidget>

// BifrostDiffView shows two full ViewFrames side by side (each with BN's
// native file-type / view-type / IL-type combo toolbar).  The diff function
// list is displayed in the BifrostSidebar (RightBottom) rather than inline.
// Navigation and block/instruction highlighting are driven entirely by clicks
// on that sidebar list — this view has no toolbar of its own. Use the ViewFrame
// headers' own view-type control to switch a pane to the CFG graph.
class BifrostDiffView : public QWidget
{
    Q_OBJECT

    QSplitter* m_frameSplit = nullptr;

    ViewFrame*       m_leftFrame   = nullptr;
    ViewFrame*       m_rightFrame  = nullptr;
    SplitPaneWidget* m_leftWidget  = nullptr;
    SplitPaneWidget* m_rightWidget = nullptr;

    BinaryNinja::Ref<BinaryNinja::BinaryView> m_leftBv;
    BinaryNinja::Ref<BinaryNinja::BinaryView> m_rightBv;
    QString m_leftBvName;
    QString m_rightBvName;

    // The diff this view shows, kept so the view can re-register itself as the
    // active diff whenever its tab is shown (so multiple diff views coexist).
    BinaryNinja::Ref<BinaryNinja::Metadata> m_diffData;

    // Register this view as the active diff driver (nav callbacks + diff data).
    void registerActive();

    BinaryNinja::Ref<BinaryNinja::Function> m_prevLeftFunc;
    BinaryNinja::Ref<BinaryNinja::Function> m_prevRightFunc;

    // Auto-open-from-project state: openProjectFile is async, so we open the
    // missing binaries once and retry building the panes on a bounded timer.
    bool m_autoOpenTried = false;
    int  m_resolveAttempts = 0;

    // Instruction addresses currently highlighted, so they can be cleared on
    // the next navigation without scanning the whole function.
    std::vector<uint64_t> m_leftInstrHi;
    std::vector<uint64_t> m_rightInstrHi;

    static std::string blockSignature(BinaryNinja::Ref<BinaryNinja::BasicBlock> block);
    static QString     bvDisplayName(BinaryNinja::Ref<BinaryNinja::BinaryView> bv);
    BinaryNinja::Ref<BinaryNinja::BinaryView> findBvByName(const QString& name) const;

    static FileContext* fileContextForBv(BinaryNinja::Ref<BinaryNinja::BinaryView> bv);

    // True when it is safe to set/clear user highlights on functions in `bv`:
    // its pane `frame` is built AND the BinaryView's current view frame has a
    // materialized widget. BN's highlight-changed refresh calls
    // FileContext::GetCurrentView() → ViewFrame::getTypeForView(getCurrentWidget())
    // and does NOT null-check the widget, so highlighting a binary whose view
    // isn't materialized yet (e.g. still auto-opening) crashes inside BN.
    bool sideHighlightSafe(ViewFrame* frame,
                           BinaryNinja::Ref<BinaryNinja::BinaryView> bv) const;

    SplitPaneWidget* makeSplitPane(BinaryNinja::Ref<BinaryNinja::BinaryView> bv,
                                   ViewFrame*& frameOut);
    void initPanes();
    // Open any still-missing binaries from the project (async) and schedule a
    // bounded retry of initPanes so their panes appear once loaded.
    void ensureBinariesOpen();
    static QString normalizeName(QString n);

public:
    explicit BifrostDiffView(QWidget* parent,
                             BinaryNinja::Ref<BinaryNinja::Metadata> diffData,
                             const QString& diffName);
    virtual ~BifrostDiffView() override;

    // Called by the sidebar when the user clicks a diff entry.
    void navigateToEntry(uint64_t leftAddr, uint64_t rightAddr, const QString& status);

protected:
    void showEvent(QShowEvent* event) override;

public:

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
