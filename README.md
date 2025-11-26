# String idea

## Overall ideas

### Aho-Corasick idea

- Partition JOIN_STRINGS by fingerprint for better load balancing: `(row.fp & substring_fp) == substring_fp`
- Row.title can only contains a pattern only if (row.fp & substring_fp) == substring_fp
- This also applies to partitions' fingerprint as well
  => We can easily parallelize Trie construction by partitioning according to fingerprint
- Which also means, we can use full 64-bits fingerprint with full utf8 support, and no need to index it before

**Four criteria to answer**:
- Cache-aware trie -- currently evaluating ART index
- Wildcard matching:
  - In the trie indexing's output link, beside storing pattern row-id, also store the matching offset within the pattern
  - Use the offset to answer wildcard `_` and `%`:
    - E.g., for `WHERE col LIKE "green % red"`, offset of `green` should be < offset of `red`
- If pattern >> data, is Aho-Corasick is still the best solution?
  - Consider Wu-manber algorithm
- Parallelize trie construction, especially when # patterns is large
  - Is ART with optimistic lock coupling good enough?
  - Partitioning based on fingerprint?

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

### Target workloads

#### First problem

- Wildcard filtering, e.g., `WHERE col LIKE "%green%"`
  - Bloom filter on all string columns
  - Steal some ideas from here: www.rbanno.net/data/paper/202501_IEEE_CCNC.pdf
- Join on full string
  - Use a dictionary compression, e.g., OnPair, to shorten the string value used in join predicate
  - Or use a per-column dictionary for compression/quick-search
    - Upon join/filter, use the corresponding dictionary

#### Second problem (To clarify)

- Peter's USSR paper
- Join on substring, something regex/full-text-search like
  - Prefix, suffix, middle-of-string

#### Example

Mostly follow the discussion here: https://cedardb.com/docs/example_datasets/job/

- `LIKE` filtering & join
  - Join on constant string
  - Join on another relation
- (Sub)string matching

**Example for `LIKE Join on another relation`**

Two relations: R and S
```
R.name  |   S.name_pattern
Duy     |   ikt
Viktor  |
Till    |
Maxi    |
```

`Query`: `SELECT * FROM R, S WHERE R.name LIKE '% S.name_pattern %'`

## Dependencies

### Core

`sudo apt-get install autoconf automake libtool curl make cmake g++ libgtest-dev libgmock-dev`

### Compilation

`mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .. && make -j`

### Dataset

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
