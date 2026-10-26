// Wc3Particles2 UV-animation intervals (head/tail lifespan and decay).
//
// MDX stores each as {start, end, repeat} (WhiteoutFlakes MdxModelAdapter:
// [0] start, [1] end, [2] repeat). The emitter panel shows "Start / End /
// Repeat" with "End" bound to the *End param and "Repeat" to the *Repeat
// param (range 1..255). The importer used to write interval[1] into *Repeat
// and interval[2] into *End, and the exporter and the preview read them back
// the same way - a lossless round trip, but the panel showed End and Repeat
// swapped, so editing "End" changed the repeat count.
//
// Imports made since the fix carry the user property Wc3P2Intervals=1 and
// hold the values where the panel shows them. An imported emitter without it
// (recognised by the importer's Billboarded user property) keeps the old
// order, so existing scenes still export and preview as before. An emitter
// the user created and typed in holds the panel's meaning.
#pragma once

#include <max.h>

namespace wdx::p2 {

inline bool legacyIntervalOrder(INode* node)
{
    if (!node) return false;
    int v = 0;
    if (node->GetUserPropInt(_T("Wc3P2Intervals"), v)) return false;
    int b = 0;
    return node->GetUserPropInt(_T("Billboarded"), b) != FALSE;
}

inline void markIntervalOrder(INode* node)
{
    if (node) node->SetUserPropInt(_T("Wc3P2Intervals"), 1);
}

} // namespace wdx::p2
