//
// Created by florian on 18.11.15.
//

#ifndef ART_OPTIMISTICLOCK_COUPLING_N_H
#define ART_OPTIMISTICLOCK_COUPLING_N_H

#include "art/node.h"

#include <functional>

namespace ART_OLC {

class Tree {
 public:
  using LoadKeyFunction                = std::function<void(TupleID tid, Key &key)>;
  using CheckKeyFunction               = std::function<bool(const TupleID tid, const Key &key)>;
  static constexpr TupleID INVALID_TID = std::numeric_limits<TupleID>::max();

 private:
  N *const root;
  LoadKeyFunction loadKey;
  CheckKeyFunction checkKey;
  Epoche epoche{256};

 public:
  enum class CheckPrefixResult : uint8_t { Match, NoMatch, OptimisticMatch };

  enum class CheckPrefixPessimisticResult : uint8_t {
    Match,
    NoMatch,
  };

  enum class PCCompareResults : uint8_t {
    Smaller,
    Equal,
    Bigger,
  };
  enum class PCEqualsResults : uint8_t { BothMatch, Contained, NoMatch };

  static inline CheckPrefixResult checkPrefix(N *n, const Key &k, uint32_t &level) {
    if (n->hasPrefix()) {
      if (k.getKeyLen() <= level + n->getPrefixLength()) { return CheckPrefixResult::NoMatch; }
      for (uint32_t i = 0; i < std::min(n->getPrefixLength(), maxStoredPrefixLength); ++i) {
        if (n->getPrefix()[i] != k[level]) { return CheckPrefixResult::NoMatch; }
        ++level;
      }
      if (n->getPrefixLength() > maxStoredPrefixLength) {
        level = level + (n->getPrefixLength() - maxStoredPrefixLength);
        return CheckPrefixResult::OptimisticMatch;
      }
    }
    return CheckPrefixResult::Match;
  }

  static CheckPrefixPessimisticResult checkPrefixPessimistic(N *n, const Key &k, uint32_t &level,
                                                             uint8_t &nonMatchingKey, Prefix &nonMatchingPrefix,
                                                             LoadKeyFunction loadKey, bool &needRestart) {
    if (n->hasPrefix()) {
      uint32_t prevLevel = level;
      Key kt;
      for (uint32_t i = 0; i < n->getPrefixLength(); ++i) {
        if (i == maxStoredPrefixLength) {
          auto anyTupleID = N::getAnyChildTupleID(n, needRestart);
          if (needRestart) return CheckPrefixPessimisticResult::Match;
          loadKey(anyTupleID, kt);
        }
        uint8_t curKey = i >= maxStoredPrefixLength ? kt[level] : n->getPrefix()[i];
        if (curKey != k[level]) {
          nonMatchingKey = curKey;
          if (n->getPrefixLength() > maxStoredPrefixLength) {
            if (i < maxStoredPrefixLength) {
              auto anyTupleID = N::getAnyChildTupleID(n, needRestart);
              if (needRestart) return CheckPrefixPessimisticResult::Match;
              loadKey(anyTupleID, kt);
            }
            memcpy(nonMatchingPrefix, &kt[0] + level + 1,
                   std::min((n->getPrefixLength() - (level - prevLevel) - 1), maxStoredPrefixLength));
          } else {
            memcpy(nonMatchingPrefix, n->getPrefix() + i + 1, n->getPrefixLength() - i - 1);
          }
          return CheckPrefixPessimisticResult::NoMatch;
        }
        ++level;
      }
    }
    return CheckPrefixPessimisticResult::Match;
  }

  static PCCompareResults checkPrefixCompare(const N *n, const Key &k, uint8_t fillKey, uint32_t &level,
                                             LoadKeyFunction loadKey, bool &needRestart) {
    if (n->hasPrefix()) {
      Key kt;
      for (uint32_t i = 0; i < n->getPrefixLength(); ++i) {
        if (i == maxStoredPrefixLength) {
          auto anyTupleID = N::getAnyChildTupleID(n, needRestart);
          if (needRestart) return PCCompareResults::Equal;
          loadKey(anyTupleID, kt);
        }
        uint8_t kLevel = (k.getKeyLen() > level) ? k[level] : fillKey;

        uint8_t curKey = i >= maxStoredPrefixLength ? kt[level] : n->getPrefix()[i];
        if (curKey < kLevel) {
          return PCCompareResults::Smaller;
        } else if (curKey > kLevel) {
          return PCCompareResults::Bigger;
        }
        ++level;
      }
    }
    return PCCompareResults::Equal;
  }

  static PCEqualsResults checkPrefixEquals(const N *n, uint32_t &level, const Key &start, const Key &end,
                                           LoadKeyFunction loadKey, bool &needRestart) {
    if (n->hasPrefix()) {
      Key kt;
      for (uint32_t i = 0; i < n->getPrefixLength(); ++i) {
        if (i == maxStoredPrefixLength) {
          auto anyTupleID = N::getAnyChildTupleID(n, needRestart);
          if (needRestart) return PCEqualsResults::BothMatch;
          loadKey(anyTupleID, kt);
        }
        uint8_t startLevel = (start.getKeyLen() > level) ? start[level] : 0;
        uint8_t endLevel   = (end.getKeyLen() > level) ? end[level] : 255;

        uint8_t curKey = i >= maxStoredPrefixLength ? kt[level] : n->getPrefix()[i];
        if (curKey > startLevel && curKey < endLevel) {
          return PCEqualsResults::Contained;
        } else if (curKey < startLevel || curKey > endLevel) {
          return PCEqualsResults::NoMatch;
        }
        ++level;
      }
    }
    return PCEqualsResults::BothMatch;
  }

 public:
  Tree(LoadKeyFunction loadKey, CheckKeyFunction checkKey);

  Tree(const Tree &) = delete;

  Tree(Tree &&t) : root(t.root), loadKey(t.loadKey) {}

  ~Tree();

  ThreadInfo getThreadInfo();

  TupleID lookup(const Key &k, ThreadInfo &threadEpocheInfo) const;

  void insert(const Key &k, TupleID TupleID, ThreadInfo &epocheInfo);

  void remove(const Key &k, TupleID TupleID, ThreadInfo &epocheInfo);
};

}  // namespace ART_OLC

#endif  // ART_OPTIMISTICLOCK_COUPLING_N_H
