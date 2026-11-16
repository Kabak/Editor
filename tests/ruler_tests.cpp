// Plain C++ tests for the Qt-independent part of the Find overview ruler (no Qt needed).
#include "matchindex.h"
#include <cstdint>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAILED line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main() {
    // Row mapping: first line at the top, last line on the last row, never out of range, monotonic.
    for (int lines : {1, 2, 3, 10, 999, 100000, 2000000000}) {
        for (int height : {1, 2, 17, 600, 1440}) {
            CHECK(MatchIndex::rowForLine(0, lines, height) == 0);
            CHECK(MatchIndex::rowForLine(lines - 1, lines, height) == height - 1 || lines < height);
            CHECK(MatchIndex::rowForLine(lines - 1, lines, height) < height);
            CHECK(MatchIndex::rowForLine(-5, lines, height) == 0);
            CHECK(MatchIndex::rowForLine(lines + 50, lines, height) < height);
            int last = 0;
            for (int i = 0; i < 200; ++i) {
                const int line = static_cast<int>(static_cast<std::int64_t>(lines - 1) * i / 199);
                const int row = MatchIndex::rowForLine(line, lines, height);
                CHECK(row >= last && row >= 0 && row < height);
                last = row;
            }
        }
    }
    // A click on a row lands on a line that is drawn on that same row.
    for (int lines : {50, 1000, 123457}) {
        for (int height : {20, 333, 800}) {
            for (int row = 0; row < height; ++row) {
                const int line = MatchIndex::lineForRow(row, lines, height);
                CHECK(line >= 0 && line < lines);
                if (lines >= height) CHECK(MatchIndex::rowForLine(line, lines, height) == row);
            }
        }
    }
    CHECK(MatchIndex::lineForRow(-3, 100, 50) == MatchIndex::lineForRow(0, 100, 50));
    CHECK(MatchIndex::lineForRow(500, 100, 50) == MatchIndex::lineForRow(49, 100, 50));
    // Degenerate sizes do not divide by zero.
    CHECK(MatchIndex::rowForLine(5, 0, 100) == 0);
    CHECK(MatchIndex::rowForLine(5, 100, 0) == 0);
    CHECK(MatchIndex::lineForRow(5, 0, 100) == 0);
    CHECK(MatchIndex::lineForRow(5, 100, 0) == 0);

    // Counting and row bitmap.
    MatchIndex index;
    CHECK(index.total() == 0 && index.lines() == 0);
    const std::uint64_t empty = index.generation();
    index.add(0, 2);
    index.add(5, 1);
    index.add(99, 4);
    index.add(7, 0);    // lines without matches are ignored
    index.add(8, -1);
    CHECK(index.total() == 7);
    CHECK(index.lines() == 3);
    CHECK(index.generation() != empty);
    std::vector<std::uint8_t> rows;
    index.rows(100, 10, &rows);
    CHECK(rows.size() == 10);
    CHECK(rows[0] == 1 && rows[9] == 1);
    int marked = 0;
    for (auto flag : rows) marked += flag;
    CHECK(marked == 2);          // lines 0 and 5 both fall on row 0, line 99 on row 9
    index.rows(100, 100, &rows); // one row per line: three separate rows
    marked = 0;
    for (auto flag : rows) marked += flag;
    CHECK(marked == 3 && rows[0] == 1 && rows[5] == 1 && rows[99] == 1);
    index.rows(100, 0, &rows);
    CHECK(rows.empty());
    // Many matches in a huge document stay cheap and in range.
    MatchIndex big;
    for (int line = 0; line < 200000; ++line) big.add(line * 10, 1);
    big.rows(2000000, 700, &rows);
    marked = 0;
    for (auto flag : rows) marked += flag;
    CHECK(rows.size() == 700 && marked == 700);
    const std::uint64_t before = big.generation();
    big.clear();
    CHECK(big.total() == 0 && big.lines() == 0 && big.generation() != before);

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("ruler tests passed\n");
    return 0;
}
