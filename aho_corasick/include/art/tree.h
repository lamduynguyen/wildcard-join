//
// Created by florian on 18.11.15.
//

#ifndef ART_OPTIMISTICLOCK_COUPLING_N_H
#define ART_OPTIMISTICLOCK_COUPLING_N_H

#include "art/node.h"

#include <functional>

namespace ART {

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
  Tree(LoadKeyFunction loadKey, CheckKeyFunction checkKey);

  Tree(const Tree &) = delete;

  Tree(Tree &&t) : root(t.root) {}

  ~Tree();

  ThreadInfo getThreadInfo();

  TupleID lookup(const Key &k, ThreadInfo &threadEpocheInfo) const;

  void insert(const Key &k, TupleID TupleID, ThreadInfo &epocheInfo);
};

}  // namespace ART

#endif  // ART_OPTIMISTICLOCK_COUPLING_N_H
