#pragma once

#include "binaryninjaapi.h"

#include <functional>
#include <vector>

// Shared pane state updated by BifrostContainer / BifrostDiffView and read by
// BifrostSidebarWidget. Decouples components without direct references.
struct BifrostPaneState
{
    BinaryNinja::Ref<BinaryNinja::BinaryView> leftData;
    BinaryNinja::Ref<BinaryNinja::BinaryView> rightData;

    // Navigation callbacks registered by the active BifrostContainer or
    // BifrostDiffView so other components can drive the panes.
    std::function<void(uint64_t)> navigateLeft;
    std::function<void(uint64_t)> navigateRight;

    // Registered by BifrostDiffView: navigate BOTH panes to a diff entry and
    // apply block/instruction highlighting for it. When set, the sidebar uses
    // this instead of navigateLeft/navigateRight so highlights are applied.
    // Args: leftAddr, rightAddr, status ("identical"|"changed"|"added"|"removed").
    std::function<void(uint64_t, uint64_t, const std::string&)> highlightEntry;

    // Active diff metadata — set by BifrostDiffView or runDiff, read by sidebar.
    BinaryNinja::Ref<BinaryNinja::Metadata> activeDiffData;

    // Observers notified when activeDiffData changes.
    std::vector<std::function<void()>> diffObservers;

    void notifyDiffChanged()
    {
        for (auto& fn : diffObservers)
            if (fn) fn();
    }

    static BifrostPaneState& instance()
    {
        static BifrostPaneState s;
        return s;
    }

    void set(BinaryNinja::Ref<BinaryNinja::BinaryView> left,
             BinaryNinja::Ref<BinaryNinja::BinaryView> right)
    {
        leftData  = left;
        rightData = right;
    }

    void clearCallbacks()
    {
        navigateLeft   = nullptr;
        navigateRight  = nullptr;
        highlightEntry = nullptr;
    }

private:
    BifrostPaneState() = default;
};
