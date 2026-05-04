#pragma once
// ============================================================================
// Minimal SLK (Microsoft Multiplan / Excel exchange) parser — just enough to
// read Reforged's TerrainArt/*.slk tables. SLK is line-oriented:
//   ID;P<creator>;N;E
//   B;Y<rows>;X<cols>;D0      bounds
//   C;Y<row>;X<col>;K<value>  cell at row,col (Y carries forward if omitted)
//   F;...                     format (ignored)
//   E                         end
// Cell values are quoted strings ("...") or bare numbers; we keep them as
// strings since the columns we care about (texFile, texDir, tileset codes)
// are textual.
// ============================================================================

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteoutDex::io {

class SlkTable {
public:
    // Row 0 = header row (the SLK row 1, 1-based). Subsequent rows
    // are data rows. Cells are empty when the SLK skips them.
    std::vector<std::vector<std::string>> rows;

    // Returns the column index of `name` (case-insensitive prefix
    // match against header row 0), or -1 if not found.
    int FindColumn(std::string_view name) const;

    // Cell access with bounds checks. Returns empty string for out-
    // of-range coordinates, the SLK's empty cells, or unparsed rows.
    std::string_view Cell(size_t row, int col) const;

    size_t RowCount() const { return rows.size(); }
    size_t HeaderCount() const { return rows.empty() ? 0 : rows[0].size(); }
};

SlkTable ParseSlk(std::span<const char> bytes);
SlkTable ParseSlk(std::span<const uint8_t> bytes);

} // namespace WhiteoutDex::io
