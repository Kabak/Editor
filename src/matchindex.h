#pragma once
// Qt-independent bookkeeping for the Find overview ruler: which lines contain a match, and how
// a line number maps to a pixel row of the ruler (and back). Header-only so it can be tested
// without Qt (tests/ruler_tests.cpp).
#include <cstddef>
#include <cstdint>
#include <vector>

class MatchIndex {
public:
    void clear() { lines_.clear(); total_ = 0; ++generation_; }
    // Lines must be added in ascending order, once each; count is the number of matches on it.
    void add(int line, int count) {
        if (count <= 0) return;
        lines_.push_back(line);
        total_ += count;
        ++generation_;
    }
    std::int64_t total() const { return total_; }
    std::size_t lines() const { return lines_.size(); }
    const std::vector<int> &lineNumbers() const { return lines_; }
    // Changes whenever the content changes, so a cached bitmap knows when it is stale.
    std::uint64_t generation() const { return generation_; }

    // Pixel row (0 .. height-1) that shows the given line of a document with lineCount lines.
    static int rowForLine(int line, int lineCount, int height) {
        if (height <= 0 || lineCount <= 0) return 0;
        if (line < 0) line = 0;
        if (line >= lineCount) line = lineCount - 1;
        return static_cast<int>(static_cast<std::int64_t>(line) * height / lineCount);
    }
    // The first line that is drawn on a pixel row (rowForLine of it gives the row back).
    static int lineForRow(int row, int lineCount, int height) {
        if (height <= 0 || lineCount <= 0) return 0;
        if (row < 0) row = 0;
        if (row >= height) row = height - 1;
        const std::int64_t line = (static_cast<std::int64_t>(row) * lineCount + height - 1) / height;
        return static_cast<int>(line >= lineCount ? lineCount - 1 : line);
    }
    // One flag per pixel row: 1 when at least one line shown on that row has a match.
    void rows(int lineCount, int height, std::vector<std::uint8_t> *out) const {
        out->assign(height > 0 ? static_cast<std::size_t>(height) : 0, 0);
        if (out->empty()) return;
        for (int line : lines_) (*out)[static_cast<std::size_t>(rowForLine(line, lineCount, height))] = 1;
    }
private:
    std::vector<int> lines_;
    std::int64_t total_ = 0;
    std::uint64_t generation_ = 0;
};
