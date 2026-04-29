// ============================================================================
// Minimal SLK parser — see slk.h for the format summary.
// ============================================================================

#include "slk.h"

#include <cctype>
#include <string_view>

namespace WhiteoutDex::io {

namespace {

// Iterate semicolon-separated fields in a line, skipping the leading
// record-type letter the caller already consumed.
struct Lexer {
    std::string_view line;
    size_t           pos = 0;

    bool empty() const { return pos >= line.size(); }

    // Advance past the next ';' (or end-of-line). Returns the field
    // contents (excluding the trailing semicolon).
    std::string_view next() {
        if (pos >= line.size()) return {};
        const size_t start = pos;
        while (pos < line.size() && line[pos] != ';') ++pos;
        std::string_view field = line.substr(start, pos - start);
        if (pos < line.size()) ++pos;  // consume ';'
        return field;
    }
};

bool ParseInt(std::string_view s, int& out) {
    if (s.empty()) return false;
    int sign = 1;
    size_t i = 0;
    if (s[0] == '-') { sign = -1; i = 1; }
    int v = 0;
    bool any = false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
        any = true;
    }
    if (!any) return false;
    out = sign * v;
    return true;
}

// Strip leading 'K' marker and surrounding double-quotes from a cell
// value field. SLK escapes embedded quotes by doubling them ("").
std::string DecodeCellValue(std::string_view field) {
    if (field.empty() || field[0] != 'K') return std::string(field);
    std::string_view body = field.substr(1);
    if (body.size() >= 2 && body.front() == '"' && body.back() == '"') {
        std::string out;
        out.reserve(body.size() - 2);
        for (size_t i = 1; i + 1 < body.size(); ++i) {
            if (body[i] == '"' && i + 1 < body.size() - 1 && body[i + 1] == '"') {
                out += '"';
                ++i;
            } else {
                out += body[i];
            }
        }
        return out;
    }
    return std::string(body);
}

bool IEqual(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    }
    return true;
}

void EnsureCell(std::vector<std::vector<std::string>>& rows, int rowIdx, int colIdx) {
    if (rowIdx < 0 || colIdx < 0) return;
    if ((size_t)rowIdx >= rows.size()) rows.resize(rowIdx + 1);
    auto& r = rows[rowIdx];
    if ((size_t)colIdx >= r.size()) r.resize(colIdx + 1);
}

} // namespace

int SlkTable::FindColumn(std::string_view name) const {
    if (rows.empty()) return -1;
    const auto& header = rows[0];
    for (int i = 0; i < (int)header.size(); ++i) {
        if (IEqual(header[i], name)) return i;
    }
    return -1;
}

std::string_view SlkTable::Cell(size_t row, int col) const {
    if (col < 0 || row >= rows.size()) return {};
    const auto& r = rows[row];
    if ((size_t)col >= r.size()) return {};
    return r[col];
}

SlkTable ParseSlk(std::span<const char> bytes) {
    SlkTable table;
    int curRow = 1;  // SLK 1-based; row 1 = header
    int curCol = 1;

    // Walk the buffer line by line.
    size_t i = 0;
    while (i < bytes.size()) {
        size_t lineStart = i;
        while (i < bytes.size() && bytes[i] != '\n' && bytes[i] != '\r') ++i;
        std::string_view line(&bytes[lineStart], i - lineStart);
        // Skip CR/LF
        while (i < bytes.size() && (bytes[i] == '\n' || bytes[i] == '\r')) ++i;
        if (line.empty()) continue;
        const char rec = line.front();
        if (rec == 'E') break;
        if (rec != 'C') continue;  // ID/B/F/P/etc — we only care about cell records

        Lexer lx{line};
        lx.next();  // consume the leading "C"
        std::string value;
        bool sawValue = false;
        int rowOverride = curRow;
        int colOverride = curCol;
        while (!lx.empty()) {
            std::string_view field = lx.next();
            if (field.empty()) continue;
            const char tag = field.front();
            std::string_view body = field.substr(1);
            switch (tag) {
                case 'Y': {
                    int y; if (ParseInt(body, y)) rowOverride = y;
                    break;
                }
                case 'X': {
                    int x; if (ParseInt(body, x)) colOverride = x;
                    break;
                }
                case 'K': {
                    value = DecodeCellValue(field);
                    sawValue = true;
                    break;
                }
                default:
                    break;  // ignore E/G/R/S/etc
            }
        }
        curRow = rowOverride;
        curCol = colOverride;
        if (sawValue) {
            // Convert from 1-based SLK coordinates to 0-based table.
            EnsureCell(table.rows, curRow - 1, curCol - 1);
            table.rows[curRow - 1][curCol - 1] = std::move(value);
        }
    }
    return table;
}

SlkTable ParseSlk(std::span<const uint8_t> bytes) {
    return ParseSlk(std::span<const char>(reinterpret_cast<const char*>(bytes.data()),
                                           bytes.size()));
}

} // namespace WhiteoutDex::io
