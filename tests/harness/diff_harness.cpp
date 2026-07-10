/*
 * diff_harness — headless end-to-end test for the Bifrost diff engine.
 *
 * Loads two binaries through Binary Ninja's core (no UI), runs
 * bifrost::DiffEngine::Compute, prints the result and asserts the expected
 * outcomes for the target_v1 / target_v2 fixtures. Exit code 0 = all checks
 * passed, 1 = a check failed, 2 = setup error.
 */

#include "binaryninjacore.h"
#include "binaryninjaapi.h"

#include "bifrostdiffengine.h"

#include <algorithm>
#include <iostream>
#include <string>

using namespace BinaryNinja;

// Compare ignoring a leading underscore (Mach-O prefixes symbols with '_').
static bool sameSym(const std::string& a, const std::string& b)
{
    auto strip = [](const std::string& s) {
        return (!s.empty() && s[0] == '_') ? s.substr(1) : s;
    };
    return strip(a) == strip(b);
}

static const bifrost::FuncMatch* byLeft(const std::vector<bifrost::FuncMatch>& v,
                                        const std::string& name)
{
    for (auto& fm : v) if (sameSym(fm.leftName, name)) return &fm;
    return nullptr;
}
static const bifrost::FuncMatch* byRight(const std::vector<bifrost::FuncMatch>& v,
                                         const std::string& name)
{
    for (auto& fm : v) if (sameSym(fm.rightName, name)) return &fm;
    return nullptr;
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: " << argv[0] << " <v1.dylib> <v2.dylib>\n";
        return 2;
    }

    SetBundledPluginDirectory(GetBundledPluginDirectory());
    InitPlugins();

    Ref<BinaryView> a = Load(argv[1]);
    Ref<BinaryView> b = Load(argv[2]);
    if (!a || !b || a->GetTypeName() == "Raw" || b->GetTypeName() == "Raw")
    {
        std::cerr << "failed to load one or both inputs as executables\n";
        return 2;
    }
    a->UpdateAnalysisAndWait();
    b->UpdateAnalysisAndWait();

    bifrost::DiffEngine engine;
    bifrost::DiffResult diff = engine.Compute(a, b);

    auto fns = diff.functions;
    std::sort(fns.begin(), fns.end(), [](const bifrost::FuncMatch& x, const bifrost::FuncMatch& y) {
        const std::string& nx = !x.leftName.empty() ? x.leftName : x.rightName;
        const std::string& ny = !y.leftName.empty() ? y.leftName : y.rightName;
        return nx < ny;
    });

    std::cout << "summary: identical=" << diff.identical
              << " changed=" << diff.changed
              << " added=" << diff.added
              << " removed=" << diff.removed << "\n";
    for (auto& fm : fns)
    {
        const std::string& nm = !fm.leftName.empty() ? fm.leftName : fm.rightName;
        std::printf("  %-9s sim=%.2f conf=%.2f [%-16s] %s",
                    bifrost::MatchStatusString(fm.status), fm.similarity, fm.confidence,
                    fm.algorithm.c_str(), nm.c_str());
        if (fm.leftName != fm.rightName && !fm.leftName.empty() && !fm.rightName.empty())
            std::printf("  (%s -> %s)", fm.leftName.c_str(), fm.rightName.c_str());
        std::printf("  bb[=%d ~%d +%d -%d]\n", fm.bbIdentical, fm.bbChanged, fm.bbAdded, fm.bbRemoved);
    }

    int fail = 0;
    auto check = [&](bool cond, const std::string& msg) {
        std::cout << (cond ? "  PASS  " : "  FAIL  ") << msg << "\n";
        if (!cond) ++fail;
    };
    auto isMatched = [](const bifrost::FuncMatch* m) {
        return m && (m->status == bifrost::MatchStatus::Identical
                  || m->status == bifrost::MatchStatus::Changed);
    };

    std::cout << "checks:\n";

    // Unchanged functions should match.
    check(isMatched(byLeft(fns, "find_pattern")),  "find_pattern matches");
    check(isMatched(byLeft(fns, "encode_base16")), "encode_base16 matches");

    // Changed-but-same-name functions should match as Changed.
    const bifrost::FuncMatch* xr = byLeft(fns, "xor_encrypt");
    check(xr && xr->status == bifrost::MatchStatus::Changed, "xor_encrypt is Changed");
    const bifrost::FuncMatch* cs = byLeft(fns, "checksum");
    check(cs && cs->status == bifrost::MatchStatus::Changed, "checksum is Changed");

    // Renamed-but-identical: clear_buffer(v1) must pair with zero_buffer(v2),
    // proving the match does not depend on the symbol name.
    const bifrost::FuncMatch* rn = byLeft(fns, "clear_buffer");
    check(rn && sameSym(rn->rightName, "zero_buffer") && isMatched(rn),
          "clear_buffer -> zero_buffer matched by structure (name-independent)");

    // Reordered blocks: classify matches (same name), structurally.
    check(isMatched(byLeft(fns, "classify")), "classify matches across block reorder");

    // Deletions.
    const bifrost::FuncMatch* d1 = byLeft(fns, "decode_base16");
    const bifrost::FuncMatch* d2 = byLeft(fns, "reverse_bytes");
    check(d1 && d1->status == bifrost::MatchStatus::Removed, "decode_base16 is Removed");
    check(d2 && d2->status == bifrost::MatchStatus::Removed, "reverse_bytes is Removed");

    // Additions.
    const bifrost::FuncMatch* a1 = byRight(fns, "popcount32");
    const bifrost::FuncMatch* a2 = byRight(fns, "rotate_left32");
    check(a1 && a1->status == bifrost::MatchStatus::Added, "popcount32 is Added");
    check(a2 && a2->status == bifrost::MatchStatus::Added, "rotate_left32 is Added");

    std::cout << (fail ? "\nRESULT: FAILED\n" : "\nRESULT: PASSED\n");
    return fail ? 1 : 0;
}
