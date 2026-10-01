#pragma once

// Replaceable texture IDs (MDX TEXS replaceableId) and the "Replaceable
// Texture" dropdown of Wc3Material / Wc3Bitmap, which stores the 1-based
// position of its entry, not the ID.
//
// The IDs the game knows (Hive Workshop, "Known replaceable IDs"):
//   0 none, 1 team color, 2 team glow, 11 cliff, 21 cursor models (no
//   texture), 31 Lordaeron tree, 32 Ashenvale tree, 33 Barrens tree,
//   34 Northrend tree, 35 mushroom tree, 36 ruins tree, 37 Outland mushroom tree.
//
// Dropdown values:
//   1..11     the entries below, in this order;
//   12..99    older imports stored "ID + 1" (cliff 12, trees 32..38);
//   100+      an ID the list does not hold, stored as "ID + 100".
// Positions 4..11 used to be exported as position - 1 (cliff as 3), which no
// game reads; they now mean what the dropdown shows.

namespace wdx::replaceable {

inline constexpr int kDropdownIds[] = {0, 1, 2, 11, 31, 32, 33, 34, 35, 36, 37};
inline constexpr int kDropdownCount = static_cast<int>(sizeof(kDropdownIds) / sizeof(kDropdownIds[0]));

// Dropdown value -> MDX replaceable ID.
inline int IdFromDropdown(int value) {
    if (value >= 1 && value <= kDropdownCount) return kDropdownIds[value - 1];
    if (value >= 100) return value - 100;
    if (value > kDropdownCount) return value - 1;  // older import: ID + 1
    return 0;
}

// MDX replaceable ID -> dropdown value.
inline int DropdownFromId(int id) {
    for (int i = 0; i < kDropdownCount; ++i)
        if (kDropdownIds[i] == id) return i + 1;
    return id < 0 ? 1 : id + 100;
}

// Where the game keeps the texture of a replaceable ID ("" for none).
inline const wchar_t* PathFromId(int id) {
    switch (id) {
    case 1:  return L"ReplaceableTextures\\TeamColor\\TeamColor00.blp";
    case 2:  return L"ReplaceableTextures\\TeamGlow\\TeamGlow00.blp";
    case 11: return L"ReplaceableTextures\\Cliff\\Cliff0.blp";
    case 31: return L"ReplaceableTextures\\LordaeronTree\\LordaeronSummerTree.blp";
    case 32: return L"ReplaceableTextures\\AshenvaleTree\\AshenTree.blp";
    case 33: return L"ReplaceableTextures\\BarrensTree\\BarrensTree.blp";
    case 34: return L"ReplaceableTextures\\NorthrendTree\\NorthTree.blp";
    case 35: return L"ReplaceableTextures\\Mushroom\\MushroomTree.blp";
    case 36: return L"ReplaceableTextures\\RuinsTree\\RuinsTree.blp";
    case 37: return L"ReplaceableTextures\\OutlandMushroomTree\\MushroomTree.blp";
    default: return L"";
    }
}

} // namespace wdx::replaceable
