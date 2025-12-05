# String idea

## Aho-Corasick-based idea

- Partition JOIN_STRINGS by fingerprint for better load balancing: `(row.fp & substring_fp) == substring_fp`
- Row.title can only contains a pattern only if (row.fp & substring_fp) == substring_fp
- This also applies to partitions' fingerprint as well
  => We can easily parallelize Trie construction by partitioning according to fingerprint
- Which also means, we can use full 64-bits fingerprint with full utf8 support, and no need to index it before

### Questions to answer

**Bold** means we have an answer to the question

- Wildcard matching:
  - In the trie indexing's output link, beside storing pattern row-id, also store the matching offset within the pattern
  - Use the offset to answer wildcard `_` and `%`:
    - E.g., for `WHERE col LIKE "green % red"`, offset of `green` should be < offset of `red`
- If pattern >> data, is Aho-Corasick is still the best solution?
  - Consider Wu-manber algorithm
- Parallelize trie construction, especially when # patterns is large
  - Initialize ART (without path compression and lazy expansion)
  - Is ART with optimistic lock coupling good enough?
  - Can we also partition based on fingerprint?
    - Maybe we can accept inserting a pattern to multiple partitioned trie

**Cache-aware trie**: ART index looks good
**Should we implement any optimization for the ART**
  - *Path compression*: Each inner node in the compressed path may point to different suffix links
    - Hence, per an inner node, we need to store all of suffix (& output) links using a vector
    - Also, suffix link calculation must consider compressed paths as single units rather than individual characters
      - I.e., suffix links under path compression now works on a chunk granularity (with arbitrary size -- depending on the compressed path) rather than character granularity, which makes the whole implementation much more complicated
    - Memory consumption should also remain the same, as we still have to maintain per-character overheads
    - The only gain is number of traversal steps on the trie
      - However, as Aho-Corasick complexity is O(text-length + pattern-size + no-matches), reducing trie traversal cost does not help much
  - *Lazy expansion*: Turn out not very helpful
    - Lazy expansion requires storing key elsewhere, not in the tree structure
    - After matching on a leaf, need to compare the substring with the original key
      - This additional step turns out to be actually very expensive, especially if our text matches multiple patterns
      - This contrast with normal trie-based aho-corasick, every single traversal step allows us to match multiple patterns at the same time, rather than each traversal step requires us to compare last suffix with the pattern's keyword again
      - Working example:
        - We have a text of `.......shadow.....dowrand`
        - We have a keyword `shadow`, with a trie of `s` -> `h` -> `a` -> [`d` => `dow`, .... other keys]
        - At 4th character of the text, we already traverse through [`s`, `h`, `a`] and reach `d` => `dow`
          - We now have to compare check if `text[4:]` matches the leaf key, which takes the same complexity with original trie
            - Only better at cache-aware; however, with small node sizes, this doesn't matter very much
          - As `dow` is a leaf node, we also lose many suffix links generated from this suffix
            - Likely have to go back to the root node => lose the performance benefits of AhoCorasick's suffix links
    - Probably we should find a better working example

### (Obsolete) Inverted index idea

- Lazily build the inverted hash index using n-grams of all rows
  - Inverted index: Map from (sub)string to document ID (row ID in our case)
  - All strings have an 1B fingerprint -- the fingerprint represents set of most frequent characters from data sampling process
  - Upon scan/filter/join, only process with strings whose fingerprint matches the required pattern
  - With join, we always have to probe the data size, which involves scanning the table
- For a substring/pattern search, generate the set of possible rows using intersection of all row IDs from inverted index
  - Example: Find substring `Doctor` => Four n-grams `Doc`, `oct`, `cto`, `tor`
  - Let's say the inverted index is `SIGN_HASHTABLE`
  - We have four sets of row IDs that contain four n-grams, respectively:
    - `Set a1 = SIGN_HASHTABLE[hash(Doc)]`
    - `Set a2 = SIGN_HASHTABLE[hash(oct)]`
    - `Set a3 = SIGN_HASHTABLE[hash(cto)]`
    - `Set a4 = SIGN_HASHTABLE[hash(tor)]`
  - The row IDs we want to evaluate are: `a1 & a2 & a3 & a4`
- With the row IDs from this intersection, we proceed with normal filter/join query

## Dependencies

### Core

`sudo apt-get install autoconf automake libtool curl make cmake g++ libgtest-dev libgmock-dev`

### Compilation

`mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .. && make -j`

### Dataset

#### IMDB

- Download `imdb` dataset as extract it into folder `imdb`

```shell
mkdir imdb && cd imdb
curl -OL https://bonsai.cedardb.com/job/imdb.tgz
tar -zxvf imdb.tgz
```

After this step, `ls ./imdb` should show multiple csv files and one sql file containing the SQL schema of all files

*CSV format errors*:
- In `imdb/title.csv`, row `2522636,\Frag'ile\,,1,2010,,F624,,,,,c0b2e279bce6d3b1717e750a2591bb6d`.
  Fix: remove two `\` characters
- Remove all escaped-comma (i.e., `\"`)

#### Amazon

- Download the dataset from this link: https://www.kaggle.com/datasets/asaniczka/amazon-uk-products-dataset-2023/data
  - Assuming the downloaded file is `archive.zip` in `~/Downloads` folder, unzip it
  - Afterward, we should have file `~/Downloads/archive/amz_uk_processed_data.csv` exists
- Extract it, copy the above csv file into `amazon` folder, and rename it to `product.csv`

```shell
mkdir amazon && cd amazon
mv ~/Downloads/archive/amz_uk_processed_data.csv product.csv
```
