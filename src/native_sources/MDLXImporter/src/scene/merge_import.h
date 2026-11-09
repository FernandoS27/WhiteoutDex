// MDLXImporter — Merge import: a model imported into a scene that already
// holds one (Import mode "Merge").
//
// The two models end up as ONE model with one sequence list, the way a unit
// with a second form or a worker with a carrying variant is built:
//   * the new model's sequences start after the last existing one (20 frames
//     gap, as between all imported sequences), frame 0 stays the bind pose;
//   * its sequence names get the name tag the user chose, Blizzard style
//     ("Stand" -> "Stand Gold", "Attack 1" -> "Attack Gold 1"), and are made
//     unique against the names already there;
//   * each model's visible objects are hidden during the other model's
//     sequences, so the game never shows both at once.
#pragma once

#include <max.h>

#include <set>
#include <string>
#include <vector>

namespace mdx_merge {

/// One sequence of the scene's sequence list (the WhiteoutDexSequenceData
/// custom attributes on the root node), times in ticks.
struct SceneSequence {
    std::wstring name;
    TimeValue start = 0;
    TimeValue end = 0;
};

/// The sequences the scene already holds. Attaches the sequence attributes
/// first (WdxSequenceStorage.ensureCA adopts a legacy NeoDex block), so a list
/// written by an older tool is read too. Empty when there is none.
std::vector<SceneSequence> readSceneSequences();

/// The name tags offered for merged sequences: the extra words Warcraft III
/// reads in an animation name (docs: Animation names). Index 0 is the empty
/// tag, which keeps the names.
const std::vector<std::wstring>& nameTags();

/// `name` with `tag` added before a trailing variant number, made unique
/// (case-insensitive) against `taken`, which receives the result:
///   "Stand" + "Gold"      -> "Stand Gold"
///   "Attack 2" + "Gold"   -> "Attack Gold 2"
///   "Stand - 2" + "Gold"  -> "Stand Gold - 2"
///   a name that already has the tag keeps it; a name still taken counts its
///   trailing number up ("Stand Gold - 2", "Stand Gold - 3", ...).
std::wstring tagSequenceName(const std::wstring& name, const std::wstring& tag,
                             std::vector<std::wstring>& taken);

/// Handles of every node in the scene.
std::set<ULONG> sceneNodeHandles(Interface* gi);

/// Hides the visible objects (meshes, emitters, ribbons, popcorn, lights,
/// attachments) of each model during the other model's sequences: the nodes in
/// `oldHandles` during `newSequences`, every other node during `oldSequences`.
/// A node's own sequences keep the visibility they had. Nodes without a
/// visibility track get a Boolean one, visible at frame 0.
void hideDuringOtherSequences(Interface* gi, const std::set<ULONG>& oldHandles,
                              const std::vector<Interval>& oldSequences,
                              const std::vector<Interval>& newSequences);

} // namespace mdx_merge
