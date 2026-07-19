#include "bifrostdiffview.h"
#include "bifrostdiffstore.h"
#include "bifrostfeatures.h"
#include "bifrostmatch.h"
#include "filecontext.h"
#include "pane.h"
#include "uicontext.h"

#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>

#include <map>

using namespace BinaryNinja;

// ── Helpers ────────────────────────────────────────────────────────────────────

/*static*/ bool BifrostDiffView::isSemanticToken(BNInstructionTextTokenType t)
{
    switch (t)
    {
    case InstructionToken: case RegisterToken: case OperandSeparatorToken:
    case TextToken: case BeginMemoryOperandToken: case EndMemoryOperandToken:
    case IntegerToken: case FloatingPointToken:
        return true;
    default: return false;
    }
}

/*static*/ std::string BifrostDiffView::blockSignature(Ref<BasicBlock> block)
{
    Ref<DisassemblySettings> settings = new DisassemblySettings();
    std::string sig;
    for (auto& line : block->GetDisassemblyText(settings))
    {
        for (auto& tok : line.tokens)
            if (isSemanticToken(tok.type)) { sig += tok.text; sig += ' '; }
        sig += '\n';
    }
    return sig;
}

/*static*/ QString BifrostDiffView::bvDisplayName(Ref<BinaryView> bv)
{
    if (!bv) return "(none)";
    auto pf = bv->GetFile()->GetProjectFile();
    if (pf) { auto n = pf->GetName(); if (!n.empty()) return QString::fromStdString(n); }
    auto p = bv->GetFile()->GetFilename();
    auto s = p.rfind('/');
    return QString::fromStdString((s != std::string::npos) ? p.substr(s + 1) : p);
}

// Strip common binary / database extensions so a diff saved against
// "foo.dylib" still resolves when the user has "foo.bndb" open.
/*static*/ QString BifrostDiffView::normalizeName(QString n)
{
    for (auto* ext : {".bndb", ".dylib", ".so", ".exe", ".dll", ".o", ".bin"})
        n.remove(ext, Qt::CaseInsensitive);
    return n;
}

Ref<BinaryView> BifrostDiffView::findBvByName(const QString& name) const
{
    const QString target = normalizeName(name);

    // Score a candidate: 2 = analyzed view whose (extension-stripped) name
    // matches, 1 = raw view match, 0 = no match. Preferring the analyzed view
    // is what makes the panes show disassembly instead of a raw hex dump.
    auto score = [&](Ref<BinaryView> bv) -> int {
        if (!bv) return 0;
        if (normalizeName(bvDisplayName(bv)) != target) return 0;
        return (bv->GetTypeName() == "Raw") ? 1 : 2;
    };

    Ref<BinaryView> best;
    int bestScore = 0;
    auto consider = [&](Ref<BinaryView> bv) {
        int s = score(bv);
        if (s > bestScore) { bestScore = s; best = bv; }
    };

    auto& state = BifrostPaneState::instance();
    consider(state.leftData);
    consider(state.rightData);

    if (UIContext* ctx = UIContext::activeContext())
        for (auto& [bv, _] : ctx->getAvailableBinaryViews())
            consider(bv);

    return best;
}

static Ref<Function> funcAtAddr(Ref<BinaryView> bv, uint64_t addr)
{
    if (!bv || !addr) return nullptr;
    auto funcs = bv->GetAnalysisFunctionsForAddress(addr);
    for (auto& f : funcs) if (f->GetStart() == addr) return f;
    if (!funcs.empty()) return funcs[0];
    return nullptr;
}

/*static*/ FileContext* BifrostDiffView::fileContextForBv(Ref<BinaryView> bv)
{
    if (!bv) return nullptr;
    auto meta = bv->GetFile();
    for (auto* fc : FileContext::getOpenFileContexts())
        if (fc->getMetadata() == meta)
            return fc;
    return nullptr;
}

SplitPaneWidget* BifrostDiffView::makeSplitPane(Ref<BinaryView> bv, ViewFrame*& frameOut)
{
    FileContext* fc = fileContextForBv(bv);
    if (!fc) return nullptr;

    frameOut = new ViewFrame(m_frameSplit, fc, "Linear");
    auto* pane = new ViewPane(frameOut);
    return new SplitPaneWidget(pane, fc);
}

// ── Construction ──────────────────────────────────────────────────────────────

BifrostDiffView::BifrostDiffView(QWidget* parent,
                                  Ref<Metadata> diffData,
                                  const QString& /* diffName */)
    : QWidget(parent)
{
    // Parse binary names so we can resolve BVs.
    if (diffData && diffData->IsKeyValueStore())
    {
        auto lm = diffData->Get("left");
        auto rm = diffData->Get("right");
        if (lm && lm->IsString()) m_leftBvName  = QString::fromStdString(lm->GetString());
        if (rm && rm->IsString()) m_rightBvName = QString::fromStdString(rm->GetString());
    }
    m_leftBv  = findBvByName(m_leftBvName);
    m_rightBv = findBvByName(m_rightBvName);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Header toolbar: Linear/Graph toggle switches both panes together.
    m_graphToggle = new QPushButton("Graph view", this);
    m_graphToggle->setCheckable(true);
    m_graphToggle->setToolTip("Toggle both panes between Linear and Control-Flow Graph views");
    connect(m_graphToggle, &QPushButton::toggled, this, [this](bool on) { setPaneViewType(on); });

    auto* reloadBtn = new QPushButton("Reload", this);
    reloadBtn->setToolTip("Re-resolve and reload both binaries (use after opening them)");
    connect(reloadBtn, &QPushButton::clicked, this, [this]() { reloadPanes(); });

    auto* header = new QHBoxLayout();
    header->setContentsMargins(4, 2, 4, 2);
    header->addWidget(m_graphToggle);
    header->addWidget(reloadBtn);
    header->addStretch(1);
    outerLayout->addLayout(header);

    // The two panes side by side — diff list lives in the sidebar.
    m_frameSplit = new QSplitter(Qt::Horizontal, this);
    m_frameSplit->setChildrenCollapsible(false);

    auto makePlaceholder = [&](const QString& bvName) -> QLabel* {
        auto* ph = new QLabel(
            QString("Loading: %1…").arg(bvName.isEmpty() ? "(unknown)" : bvName),
            m_frameSplit);
        ph->setAlignment(Qt::AlignCenter);
        ph->setWordWrap(true);
        return ph;
    };

    m_frameSplit->addWidget(makePlaceholder(m_leftBvName));
    m_frameSplit->addWidget(makePlaceholder(m_rightBvName));
    m_frameSplit->setSizes({500, 500});
    outerLayout->addWidget(m_frameSplit, 1);

    // Push the diff data into the singleton so the sidebar can display it.
    auto& state = BifrostPaneState::instance();
    state.activeDiffData = diffData;

    // Register navigate callbacks so the sidebar can drive our panes.
    state.navigateLeft = [this](uint64_t addr) {
        if (m_leftFrame && m_leftBv) m_leftFrame->navigate(m_leftBv, addr);
    };
    state.navigateRight = [this](uint64_t addr) {
        if (m_rightFrame && m_rightBv) m_rightFrame->navigate(m_rightBv, addr);
    };
    // Navigate both panes and apply diff highlighting for a clicked entry.
    state.highlightEntry = [this](uint64_t l, uint64_t r, const std::string& status) {
        navigateToEntry(l, r, QString::fromStdString(status));
    };

    // Defer pane creation until after createTabForWidget has parented us.
    QTimer::singleShot(0, this, [this, diffData]() {
        initPanes();
        // Notify sidebar now that panes are ready.
        BifrostPaneState::instance().notifyDiffChanged();
    });
}

BifrostDiffView::~BifrostDiffView()
{
    clearHighlights();

    // Clear diff state so the sidebar resets.
    auto& state = BifrostPaneState::instance();
    state.activeDiffData = nullptr;
    state.clearCallbacks();
    state.notifyDiffChanged();
}

// ── Deferred pane initialisation ──────────────────────────────────────────────

void BifrostDiffView::initPanes()
{
    if (!m_leftBv  && !m_leftBvName.isEmpty())  m_leftBv  = findBvByName(m_leftBvName);
    if (!m_rightBv && !m_rightBvName.isEmpty()) m_rightBv = findBvByName(m_rightBvName);

    // Build a side only if it isn't already built, so retries (from the async
    // auto-open) never disturb a pane that is already showing. Returns true when
    // a new ViewFrame was created for this side.
    auto buildSide = [&](Ref<BinaryView> bv, const QString& bvName,
                         ViewFrame*& frameOut, SplitPaneWidget*& widgetOut,
                         int splitterIndex) -> bool
    {
        if (frameOut) return false;   // already built — leave it alone

        QWidget* replacement = nullptr;
        bool built = false;
        if (bv)
        {
            widgetOut = makeSplitPane(bv, frameOut);
            if (widgetOut)
            {
                replacement = widgetOut;
                built = true;
                // Point the frame at the analyzed view's first function so it
                // opens on disassembly rather than a raw hex dump.
                if (frameOut)
                {
                    uint64_t addr = 0;
                    auto funcs = bv->GetAnalysisFunctionList();
                    if (!funcs.empty()) addr = funcs[0]->GetStart();
                    else                addr = bv->GetEntryPoint();
                    frameOut->navigate(bv, addr);
                }
            }
        }
        if (!replacement)
        {
            auto* ph = new QLabel(
                QString("Opening %1 from the project…\n\n"
                        "If this persists, open it manually and click Reload.")
                    .arg(bvName.isEmpty() ? "(unknown)" : bvName),
                m_frameSplit);
            ph->setAlignment(Qt::AlignCenter);
            ph->setWordWrap(true);
            replacement = ph;
        }

        QWidget* old = m_frameSplit->widget(splitterIndex);
        m_frameSplit->replaceWidget(splitterIndex, replacement);
        delete old;
        return built;
    };

    bool builtLeft  = buildSide(m_leftBv,  m_leftBvName,  m_leftFrame,  m_leftWidget,  0);
    bool builtRight = buildSide(m_rightBv, m_rightBvName, m_rightFrame, m_rightWidget, 1);

    // Kick ViewPaneHeaders to install the IL-level subtype widget (new frames only).
    auto connectKick = [](ViewFrame* frame, SplitPaneWidget* w) {
        if (!frame || !w) return;
        QObject::connect(frame, &ViewFrame::notifyViewChanged, w,
            [w](ViewFrame*) {
                w->updateStatus();
                w->enumerateViewPanes([](ViewPane* vp) {
                    vp->sendViewChange();
                    vp->updateStatus();
                });
            }, Qt::QueuedConnection);
    };
    if (builtLeft)  connectKick(m_leftFrame,  m_leftWidget);
    if (builtRight) connectKick(m_rightFrame, m_rightWidget);

    // Restore the graph/linear mode on freshly-built frames.
    if ((builtLeft || builtRight) && m_graphMode)
        setPaneViewType(true);

    // If a side is still missing, open it from the project and retry.
    ensureBinariesOpen();
}

void BifrostDiffView::ensureBinariesOpen()
{
    constexpr int kMaxResolveAttempts = 20;   // ~14s at 700ms

    bool leftMissing  = !m_leftBv  && !m_leftBvName.isEmpty();
    bool rightMissing = !m_rightBv && !m_rightBvName.isEmpty();
    if (!leftMissing && !rightMissing)
        return;   // both resolved — done

    UIContext* ctx = UIContext::activeContext();
    Ref<Project> project = ctx ? ctx->getProject() : nullptr;

    // Kick the async open(s) exactly once.
    if (ctx && project && project->IsOpen() && !m_autoOpenTried)
    {
        m_autoOpenTried = true;
        auto openMatch = [&](const QString& name) {
            const QString target = normalizeName(name);
            for (auto& pfile : project->GetFiles())
                if (normalizeName(QString::fromStdString(pfile->GetName())) == target)
                {
                    ctx->openProjectFile(pfile);
                    return;
                }
        };
        if (leftMissing)  openMatch(m_leftBvName);
        if (rightMissing) openMatch(m_rightBvName);
    }

    // Retry building the still-missing panes once the async open completes.
    if (m_resolveAttempts < kMaxResolveAttempts)
    {
        ++m_resolveAttempts;
        QTimer::singleShot(700, this, [this]() { initPanes(); });
    }
}

void BifrostDiffView::reloadPanes()
{
    // Tear down existing frames so both sides rebuild from scratch, and reset
    // the auto-open state so it can try again.
    m_leftFrame = m_rightFrame = nullptr;
    m_leftWidget = m_rightWidget = nullptr;
    m_autoOpenTried = false;
    m_resolveAttempts = 0;
    m_leftBv  = findBvByName(m_leftBvName);
    m_rightBv = findBvByName(m_rightBvName);
    initPanes();
}

void BifrostDiffView::setPaneViewType(bool graph)
{
    m_graphMode = graph;
    const QString type = graph ? "Graph" : "Linear";
    if (m_leftFrame)  m_leftFrame->setViewType(type);
    if (m_rightFrame) m_rightFrame->setViewType(type);
    if (m_graphToggle)
    {
        QSignalBlocker block(m_graphToggle);
        m_graphToggle->setChecked(graph);
        m_graphToggle->setText(graph ? "Linear view" : "Graph view");
    }
}

// ── Navigation (called by sidebar) ───────────────────────────────────────────

void BifrostDiffView::navigateToEntry(uint64_t leftAddr, uint64_t rightAddr,
                                       const QString& status)
{
    if (!m_leftBv  && !m_leftBvName.isEmpty())  m_leftBv  = findBvByName(m_leftBvName);
    if (!m_rightBv && !m_rightBvName.isEmpty()) m_rightBv = findBvByName(m_rightBvName);

    if (m_leftFrame  && leftAddr)  m_leftFrame->navigate(m_leftBv,  leftAddr);
    if (m_rightFrame && rightAddr) m_rightFrame->navigate(m_rightBv, rightAddr);

    Ref<Function> lf = funcAtAddr(m_leftBv,  leftAddr);
    Ref<Function> rf = funcAtAddr(m_rightBv, rightAddr);

    clearHighlights();

    // A matched pair (identical or changed) → per-block match highlighting.
    if ((status == "identical" || status == "changed" || status == "matched")
        && lf && rf)
    {
        applyBlockHighlights(lf, rf);
        m_prevLeftFunc = lf; m_prevRightFunc = rf;
    }
    else if (status == "removed" && lf)
    {
        for (auto& bb : lf->GetBasicBlocks()) bb->SetUserBasicBlockHighlight(RedHighlightColor);
        m_prevLeftFunc = lf;
    }
    else if (status == "added" && rf)
    {
        for (auto& bb : rf->GetBasicBlocks()) bb->SetUserBasicBlockHighlight(GreenHighlightColor);
        m_prevRightFunc = rf;
    }
}

// ── Block highlighting ────────────────────────────────────────────────────────

void BifrostDiffView::clearBlockHighlights(Ref<Function> func)
{
    if (!func) return;
    BNHighlightColor none{}; none.style = StandardHighlightColor;
    none.color = NoHighlightColor; none.alpha = 0;
    for (auto& bb : func->GetBasicBlocks()) bb->SetUserBasicBlockHighlight(none);
}

void BifrostDiffView::clearHighlights()
{
    BNHighlightColor none{}; none.style = StandardHighlightColor;
    none.color = NoHighlightColor; none.alpha = 0;

    clearBlockHighlights(m_prevLeftFunc);
    clearBlockHighlights(m_prevRightFunc);

    if (m_prevLeftFunc)
        for (uint64_t a : m_leftInstrHi)
            m_prevLeftFunc->SetUserInstructionHighlight(m_prevLeftFunc->GetArchitecture(), a, none);
    if (m_prevRightFunc)
        for (uint64_t a : m_rightInstrHi)
            m_prevRightFunc->SetUserInstructionHighlight(m_prevRightFunc->GetArchitecture(), a, none);

    m_leftInstrHi.clear();
    m_rightInstrHi.clear();
    m_prevLeftFunc = m_prevRightFunc = nullptr;
}

void BifrostDiffView::applyBlockHighlights(Ref<Function> lf, Ref<Function> rf)
{
    if (!lf || !rf || !m_leftBv || !m_rightBv) return;

    // Structural basic-block match via the engine (replaces positional zip).
    bifrost::FeatureExtractor le(m_leftBv), re(m_rightBv);
    bifrost::FuncFeatures lFeat = le.ExtractFunction(lf);
    bifrost::FuncFeatures rFeat = re.ExtractFunction(rf);
    bifrost::FuncMatch fm;
    bifrost::Matcher::MatchBlocks(lFeat, rFeat, fm);

    std::map<uint64_t, Ref<BasicBlock>> lBlocks, rBlocks;
    for (auto& b : lf->GetBasicBlocks()) lBlocks[b->GetStart()] = b;
    for (auto& b : rf->GetBasicBlocks()) rBlocks[b->GetStart()] = b;

    auto colorFor = [](bifrost::MatchStatus s) -> BNHighlightStandardColor {
        switch (s)
        {
        case bifrost::MatchStatus::Identical: return NoHighlightColor;
        case bifrost::MatchStatus::Changed:   return OrangeHighlightColor;
        case bifrost::MatchStatus::Added:     return GreenHighlightColor;
        case bifrost::MatchStatus::Removed:   return RedHighlightColor;
        }
        return NoHighlightColor;
    };

    for (auto& bm : fm.blocks)
    {
        BNHighlightStandardColor col = colorFor(bm.status);
        if (bm.leftAddr)
            if (auto it = lBlocks.find(bm.leftAddr); it != lBlocks.end())
                it->second->SetUserBasicBlockHighlight(col);
        if (bm.rightAddr)
            if (auto it = rBlocks.find(bm.rightAddr); it != rBlocks.end())
                it->second->SetUserBasicBlockHighlight(col);

        if (bm.status == bifrost::MatchStatus::Changed && bm.leftAddr && bm.rightAddr)
        {
            auto li = lBlocks.find(bm.leftAddr), ri = rBlocks.find(bm.rightAddr);
            if (li != lBlocks.end() && ri != rBlocks.end())
                highlightInstructionDiffs(lf, rf, li->second, ri->second);
        }
    }
}

// Concatenate a disassembly line's token text, dropping address tokens (which
// differ under relocation without a semantic change) so immediates still count.
static std::string normLine(const BinaryNinja::DisassemblyTextLine& line)
{
    std::string s;
    for (auto& tok : line.tokens)
    {
        if (tok.type == PossibleAddressToken || tok.type == CodeRelativeAddressToken)
            continue;
        s += tok.text;
    }
    return s;
}

void BifrostDiffView::highlightInstructionDiffs(Ref<Function> lf, Ref<Function> rf,
                                                Ref<BasicBlock> lb, Ref<BasicBlock> rb)
{
    Ref<DisassemblySettings> settings = new DisassemblySettings();
    auto ll = lb->GetDisassemblyText(settings);
    auto rl = rb->GetDisassemblyText(settings);

    Ref<Architecture> larch = lf->GetArchitecture();
    Ref<Architecture> rarch = rf->GetArchitecture();

    auto hlLeft = [&](uint64_t addr) {
        lf->SetUserInstructionHighlight(larch, addr, OrangeHighlightColor);
        m_leftInstrHi.push_back(addr);
    };
    auto hlRight = [&](uint64_t addr) {
        rf->SetUserInstructionHighlight(rarch, addr, OrangeHighlightColor);
        m_rightInstrHi.push_back(addr);
    };

    size_t shared = std::min(ll.size(), rl.size());
    for (size_t i = 0; i < shared; ++i)
        if (normLine(ll[i]) != normLine(rl[i])) { hlLeft(ll[i].addr); hlRight(rl[i].addr); }
    for (size_t i = shared; i < ll.size(); ++i) hlLeft(ll[i].addr);
    for (size_t i = shared; i < rl.size(); ++i) hlRight(rl[i].addr);
}
