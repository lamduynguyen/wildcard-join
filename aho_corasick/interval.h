#pragma once

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aho_corasick {

/**
 * Interval represents an Interval of [d_start, d_end].
 */
class Interval {
  size_t d_start;
  size_t d_end;

 public:
  Interval(size_t start, size_t end) : d_start(start), d_end(end) {}

  size_t GetStart() const { return d_start; }

  size_t GetEnd() const { return d_end; }

  size_t Size() const { return d_end - d_start + 1; }

  bool OverlapsWith(const Interval &other) const { return d_start <= other.d_end && d_end >= other.d_start; }

  bool OverlapsWith(size_t point) const { return d_start <= point && point <= d_end; }

  bool operator<(const Interval &other) const { return GetStart() < other.GetStart(); }

  bool operator!=(const Interval &other) const { return GetStart() != other.GetStart() || GetEnd() != other.GetEnd(); }

  bool operator==(const Interval &other) const { return GetStart() == other.GetStart() && GetEnd() == other.GetEnd(); }
};

/**
* The IntervalTree class is a data structure used for storing and querying Intervals.
* It's implemented as a binary tree where each node represents a range of values and contains Intervals that
overlap with that range.
* It includes:
* - IntervalCollection: This is an alias for std::vector<T>,
*       where T represents the type of Intervals stored in the tree.
* - BinaryNode: A class representing a node in the binary tree.
* 		Each node contains a point (d_point) which represents the median value of the Intervals it contains,
* 		left and right child nodes (d_left and d_right), and a collection of Intervals (d_Intervals).
* 	RemoveOverlaps(): This method removes overlapping Intervals from the given collection of Intervals.
* 	FindOverlaps(): This method finds Intervals in the tree that overlap with a given Interval i.
*/
template <typename T>
class IntervalTree {
 public:
  using IntervalCollection = std::vector<T>;

 private:
  class BinaryNode {
    enum Direction { LEFT, RIGHT };

    size_t d_point;
    std::unique_ptr<BinaryNode> d_left;
    std::unique_ptr<BinaryNode> d_right;
    IntervalCollection d_Intervals;

   public:
    explicit BinaryNode(const IntervalCollection &Intervals)
        : d_point(0), d_left(nullptr), d_right(nullptr), d_Intervals() {
      d_point = DetermineMedian(Intervals);
      IntervalCollection to_left, to_right;
      for (const auto &i : Intervals) {
        if (i.GetEnd() < d_point) {
          to_left.push_back(i);
        } else if (i.GetStart() > d_point) {
          to_right.push_back(i);
        } else {
          d_Intervals.push_back(i);
        }
      }
      if (!to_left.empty()) { d_left.reset(new BinaryNode(to_left)); }
      if (!to_right.empty()) { d_right.reset(new BinaryNode(to_right)); }
    }

    auto DetermineMedian(const IntervalCollection &Intervals) const {
      auto start = std::numeric_limits<size_t>::max();
      auto end   = std::numeric_limits<size_t>::max();
      for (const auto &i : Intervals) {
        auto cur_start = i.GetStart();
        auto cur_end   = i.GetEnd();
        if (start == std::numeric_limits<size_t>::max() || cur_start < start) { start = cur_start; }
        if (end == std::numeric_limits<size_t>::max() || cur_end > end) { end = cur_end; }
      }
      return (start + end) / 2;
    }

    IntervalCollection FindOverlaps(const T &i) {
      IntervalCollection overlaps;
      if (d_point < i.GetStart()) {
        AddToOverlaps(i, overlaps, FindOverlappingRanges(d_right, i));
        AddToOverlaps(i, overlaps, CheckRightOverlaps(i));
      } else if (d_point > i.GetEnd()) {
        AddToOverlaps(i, overlaps, FindOverlappingRanges(d_left, i));
        AddToOverlaps(i, overlaps, CheckLeftOverlaps(i));
      } else {
        AddToOverlaps(i, overlaps, d_Intervals);
        AddToOverlaps(i, overlaps, FindOverlappingRanges(d_left, i));
        AddToOverlaps(i, overlaps, FindOverlappingRanges(d_right, i));
      }
      return IntervalCollection(overlaps);
    }

   protected:
    void AddToOverlaps(const T &i, IntervalCollection &overlaps, IntervalCollection new_overlaps) const {
      for (const auto &cur : new_overlaps) {
        if (cur != i) { overlaps.push_back(cur); }
      }
    }

    IntervalCollection CheckLeftOverlaps(const T &i) const { return IntervalCollection(CheckOverlaps(i, LEFT)); }

    IntervalCollection CheckRightOverlaps(const T &i) const { return IntervalCollection(CheckOverlaps(i, RIGHT)); }

    IntervalCollection CheckOverlaps(const T &i, Direction d) const {
      IntervalCollection overlaps;
      for (const auto &cur : d_Intervals) {
        switch (d) {
          case LEFT:
            if (cur.GetStart() <= i.GetEnd()) { overlaps.push_back(cur); }
            break;
          case RIGHT:
            if (cur.GetEnd() >= i.GetStart()) { overlaps.push_back(cur); }
            break;
        }
      }
      return IntervalCollection(overlaps);
    }

    IntervalCollection FindOverlappingRanges(BinaryNode *node, const T &i) const {
      if (node) { return IntervalCollection(node->FindOverlaps(i)); }
      return IntervalCollection();
    }
  };

  BinaryNode d_root;

 public:
  explicit IntervalTree(const IntervalCollection &Intervals) : d_root(Intervals) {}

  IntervalCollection RemoveOverlaps(const IntervalCollection &Intervals) {
    IntervalCollection result(Intervals.begin(), Intervals.end());
    std::sort(result.begin(), result.end(), [](const T &a, const T &b) -> bool {
      if (b.Size() - a.Size() == 0) { return a.GetStart() > b.GetStart(); }
      return a.Size() > b.Size();
    });
    std::set<T> remove_tmp;
    for (const auto &i : result) {
      if (remove_tmp.find(i) != remove_tmp.end()) { continue; }
      auto overlaps = FindOverlaps(i);
      for (const auto &overlap : overlaps) { remove_tmp.insert(overlap); }
    }
    for (const auto &i : remove_tmp) { result.erase(std::find(result.begin(), result.end(), i)); }
    std::sort(result.begin(), result.end(), [](const T &a, const T &b) -> bool { return a.GetStart() < b.GetStart(); });
    return IntervalCollection(result);
  }

  IntervalCollection FindOverlaps(const T &i) { return IntervalCollection(d_root.FindOverlaps(i)); }
};

/**
 * The emit class is a representation of an emitted token or substring found during text parsing.
 * The emitted substring will be registered by it's Interval [start_idx, end_idx].
 * The emit list (d_emits) is stored at the end of string (terminal nodes).
 * Note:
 * 	For multiple insertion of the *same* string, the TRIE doesn't change, but the emit list *does*.
 * 	This means that if the TRIE had the string: d -> o -> g -> $ [d_emits: ['dog']]
 * 	After second insertion of the string 'dog': d -> o -> g -> $ [d_emits: ['dog', 'dog']]
 * 	THIS INFORMATION AFFECTS THE TRIE SIZE AND IS CALCULATED AS PERIPHERAL.
 */
class Emit {
 private:
  Interval d_interval;
  unsigned d_index = 0;

 public:
  Emit() : d_interval(-1, -1) {}

  Emit(size_t start, size_t end, unsigned index) : d_interval(start, end), d_index(index) {}

  unsigned Index() const { return d_index; }

  bool IsEmpty() const { return (d_interval.GetStart() == -1 && d_interval.GetEnd() == -1); }
};

}  // namespace aho_corasick