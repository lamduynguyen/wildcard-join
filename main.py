import pandas as pd
import timeit
from bitarray import bitarray
import numpy as np
from tqdm import tqdm
from functools import reduce

FINGERPRINT_SIZE = 64
FINGERPRINT_1B_SIZE = 8
NGRAM_SIZE = 3
DEBUG_RESULT = False
TITLE_DTYPE = {
    "id": "Int64",                # nullable integer (matches SQL integer)
    "title": "string",            # VARCHAR
    "imdb_index": "string",       # VARCHAR(5)
    "kind_id": "Int64",           # integer
    "production_year": "Int64",   # integer
    "imdb_id": "Int64",           # integer
    "phonetic_code": "string",    # VARCHAR(5)
    "episode_of_id": "Int64",     # integer
    "season_nr": "Int64",         # integer
    "episode_nr": "Int64",        # integer
    "series_years": "string",     # VARCHAR(49)
    "md5sum": "string"            # VARCHAR(32)
}
TITLE_HEADER = list(TITLE_DTYPE.keys())
ENGLISH_FREQ_ORDER = 'ETAOINSHRDLCUMWFGYPBVKJXQZ'
LEAST_16_FREQ = ENGLISH_FREQ_ORDER[:16] # Most frequent 16 English characters
# LEAST_16_FREQ = ENGLISH_FREQ_ORDER[-16:] # Least frequent 16 English characters: This is typically worst
# LEAST_16_FREQ = ENGLISH_FREQ_ORDER[-8:] + ENGLISH_FREQ_ORDER[:8] # Half of both worlds
VALUES = list(range(0, 8))[::-1] + list(range(0, 8))
# VALUES = list(range(0, FINGERPRINT_1B_SIZE))
CHARACTER_MAPPING = {letter: VALUES[idx] for idx, letter in enumerate(LEAST_16_FREQ)}

# Load title file
df = pd.read_csv("imdb/title.csv", names=TITLE_HEADER, dtype=TITLE_DTYPE, sep=",", on_bad_lines="warn", escapechar="\\", engine='pyarrow')
df = df[df["title"].notnull() & (df["title"].str.len() >= NGRAM_SIZE)]
df = df.reset_index(drop=True)
print(df)

# Approach #1: naive string filter
def naive_filter(df, substr: str):
  result = []
  for row in df.itertuples(index=True, name="Row"):
    for i in range(len(row.title)):
      if row.title[i:].startswith(substr):
        result.append(row)
        break
  res = pd.DataFrame(result)
  if DEBUG_RESULT:
    print(res)
  return res

# Approach #2 (TODO): global suffix array
def build_suffix_array(s: str) -> list[int]:
  if not isinstance(s, str):
    return []  # handle missing values
  return sorted(range(len(s)), key=lambda i: s[i:])

def contain_pattern(s: str, SA: list[int], substr: str):
  n = len(SA)
  l, r = 0, n - 1
  while l <= r:
    mid = (l + r) // 2
    suffix = s[SA[mid]:]
    if suffix.startswith(substr):
      return True
    elif suffix < substr:
      l = mid + 1
    else:
      r = mid - 1
  return False

def sa_filter(df, substring):
  result = []
  for row in df.itertuples(index=True, name="Row"):
    if contain_pattern(row.title, row.suffix_array, substring):
      result.append(row)
  res = pd.DataFrame(result)
  if DEBUG_RESULT:
    print(res)
  return res

# Approach #3: string fingerprint
def build_fp(s: str):
  res = bitarray(FINGERPRINT_SIZE)
  for char in s:
    res[ord(char) % FINGERPRINT_SIZE] = 1
  return res

def build_1b_fp(s: str):
  res = bitarray(FINGERPRINT_1B_SIZE)
  for char in s:
    if char.upper() in CHARACTER_MAPPING:
      res[CHARACTER_MAPPING[char.upper()]] = 1
  return res

def fp_filter(df, substr, use_full_fp = True):
  result = []
  false_positive = 0
  substring_fp = build_fp(substr) if use_full_fp else build_1b_fp(substr)
  for row in df.itertuples(index=True, name="Row"):
    if (row.fp & substring_fp) == substring_fp:
      found = False
      for i in range(len(row.title)):
        if row.title[i:].startswith(substr):
          result.append(row)
          found = True
          break
      if not found:
        false_positive += 1
  res = pd.DataFrame(result)
  print("False positive", false_positive / len(df))
  if DEBUG_RESULT:
    print(res)
  return res

# Approach #4: Hash-table-inverted-index with ngram hash signature
def preprocess_as(row, sign_hashtable, as_alphabet_size):
  ngrams = [row.title[i:i+NGRAM_SIZE] for i in range(len(row.title) - NGRAM_SIZE + 1)]
  indices = np.array([hash(ng) % as_alphabet_size for ng in ngrams])
  for sig in indices:
    sign_hashtable.setdefault(sig, set()).add(row.name)

def inverted_index_1(df, sign_hashtable, as_alphabet_size, substr):
  assert(len(substr) >= NGRAM_SIZE)
  result = []
  # ngrams = [substr[i:i+NGRAM_SIZE] for i in range(len(substr) - NGRAM_SIZE + 1)]
  ngrams = [substr[i:i+NGRAM_SIZE] for i in [0, len(substr) - NGRAM_SIZE]] # Prefix and suffix
  hashed = np.array([hash(ng) % as_alphabet_size for ng in ngrams])
  row_sets = [set(sign_hashtable[h]) for h in hashed]
  final_row_set = reduce(lambda s1, s2: s1 & s2, row_sets)
  assert(final_row_set is not None)
  for row in df.loc[list(final_row_set)].itertuples(index=True, name="Row"):
    for i in range(len(row.title)):
      if row.title[i:].startswith(substr):
        result.append(row)
        break
  res = pd.DataFrame(result)
  if DEBUG_RESULT:
    print(res)
  return res

# Approach #5: Inverted index with ngram finger print
def calculate_fp(str, start_off, leng, bitarray_format: bool = False):
  fp = bitarray(FINGERPRINT_SIZE)
  for idx in range(start_off, start_off + leng):
    fp[ord(str[idx]) % FINGERPRINT_SIZE] ^= 1
  return fp if bitarray_format else fp.to01()

def preprocess_fp(row, sign_hashtable):
  fp = np.array([calculate_fp(row.title, idx, NGRAM_SIZE) for idx in range(len(row.title))])
  for sig in fp:
    sign_hashtable.setdefault(sig, set()).add(row.name)

def inverted_index_2(df, sign_hashtable, substr):
  assert(len(substr) >= NGRAM_SIZE)
  result = []
  fp = [calculate_fp(substr, 0, NGRAM_SIZE), calculate_fp(substr, len(substr) - NGRAM_SIZE, NGRAM_SIZE)]
  row_sets = [set(sign_hashtable[h]) for h in fp]
  final_row_set = reduce(lambda s1, s2: s1 & s2, row_sets)
  assert(final_row_set is not None)
  for row in df.loc[list(final_row_set)].itertuples(index=True, name="Row"):
    for i in range(len(row.title)):
      if row.title[i:].startswith(substr):
        result.append(row)
        break
  res = pd.DataFrame(result)
  if DEBUG_RESULT:
    print(res)
  return res

# Approach #6: Use pattern's prefix and suffix fp to filter rows first, then matching rows to pattern in 2nd round
def semi_inverted_join(df, substr_list):
  ngram_map = []
  for substr in substr_list:
    prefix, suffix, space = calculate_fp(substr, 0, NGRAM_SIZE), \
      calculate_fp(substr, len(substr) - NGRAM_SIZE, NGRAM_SIZE), len(substr) - NGRAM_SIZE
    ngram_map.append((prefix, suffix, space))
  candidates = {}
  for row in df.itertuples(index=True, name="Row"):
    for i in range(len(row.title) - NGRAM_SIZE):
      fp = calculate_fp(row.title, i, NGRAM_SIZE)
      for idx, (prefix_fp, suffix_fp, space) in enumerate(ngram_map):
        if fp == prefix_fp and i + space + NGRAM_SIZE <= len(row.title) and row.Index not in candidates.get(idx, set()):
          substr_suffix_fp = calculate_fp(row.title, i + space, NGRAM_SIZE)
          if suffix_fp == substr_suffix_fp:
            candidates.setdefault(idx, set()).add(row.Index)
  return candidates

# Approach #7: First substring with approach #6 & interleave with building inverted index in approach #5
def hybrid_inverted_index(df, substr_list):
  df["fp"] = df["title"].apply(build_fp)
  first_prefix = calculate_fp(substr_list[0], 0, NGRAM_SIZE)
  result = [{} for _ in range(len(substr_list))]
  sign_hashtable = {}
  for row in df.itertuples(index=True, name="Row"):
    fp = bitarray(FINGERPRINT_SIZE)
    for idx in range(NGRAM_SIZE):
      fp[ord(row.title[idx]) % FINGERPRINT_SIZE] ^= 1
    if fp.to01() == first_prefix and row.title.startswith(substr_list[0]):
      result[0][row.Index] = row
    sign_hashtable.setdefault(fp.to01(), set()).add(row.Index)
    for i in range(1, len(row.title) - NGRAM_SIZE):
      fp[ord(row.title[i - 1]) % FINGERPRINT_SIZE] ^= 1
      fp[ord(row.title[i + NGRAM_SIZE - 1]) % FINGERPRINT_SIZE] ^= 1
      fp_number = fp.to01()
      sign_hashtable.setdefault(fp_number, set()).add(row.Index)
      if fp_number == first_prefix and row.title[i:].startswith(substr_list[0]):
        result[0][row.Index] = row
  for idx, substr in enumerate(substr_list[1:], 1):
    fp = [calculate_fp(substr, 0, NGRAM_SIZE), calculate_fp(substr, len(substr) - NGRAM_SIZE, NGRAM_SIZE)]
    row_sets = [set(sign_hashtable[h]) for h in fp]
    final_row_set = reduce(lambda s1, s2: s1 & s2, row_sets)
    assert(final_row_set is not None)
    for row in df.loc[list(final_row_set)].itertuples(index=True, name="Row"):
      for i in range(len(row.title)):
        if row.title[i:].startswith(substr):
          result[idx][row.Index] = row
          break
  if DEBUG_RESULT:
    for data in result:
      print(pd.DataFrame(data.values()))

# Approach #8: Lazily build the inverted index, hybrid with finger-printed approach
def calculate_fp2(substr, start_off, leng, bitarray_format: bool = False):
  fp = bitarray(FINGERPRINT_SIZE)
  for idx in range(start_off, start_off + leng):
    fp[ord(substr[idx]) % FINGERPRINT_SIZE] = 1
  return fp if bitarray_format else fp.to01()

def calculate_1b_fp2(substr, start_off, leng, bitarray_format: bool = False):
  fp = bitarray(FINGERPRINT_1B_SIZE)
  for idx in range(start_off, start_off + leng):
    char = substr[idx].upper()
    if char.upper() in CHARACTER_MAPPING:
      fp[CHARACTER_MAPPING[char.upper()]] = 1
  return fp if bitarray_format else fp.to01()

def hybrid_fp_filter(df, substr_list, use_full_fp = True):
  sign_hashtable = {}
  result = {}
  indexed = [False] * len(df)
  fp_calc = calculate_fp2 if use_full_fp else calculate_1b_fp2
  for substr in substr_list:
    false_positive = 0
    substring_full_fp = fp_calc(substr, 0, len(substr), True)
    super_fp = [fp_calc(substr, 0, NGRAM_SIZE), fp_calc(substr, len(substr) - NGRAM_SIZE, NGRAM_SIZE)]
    row_sets = [set(sign_hashtable.get(h, set())) for h in super_fp]
    final_row_set = reduce(lambda s1, s2: s1 & s2, row_sets)
    # Loop through rows not in indexed set
    filtered_df = (
      row for row in df.itertuples(index=True, name="Row")
      if row.Index not in final_row_set
    )
    for row in filtered_df:
      if not indexed[row.Index]:
        if (row.fp & substring_full_fp) == substring_full_fp:
          indexed[row.Index] = True
          found = False
          for i in range(len(row.title) - NGRAM_SIZE + 1):
            fp = fp_calc(row.title, i, NGRAM_SIZE)
            sign_hashtable.setdefault(fp, set()).add(row.Index)
            if row.title[i:].startswith(substr):
              found = True
          if found:
            result[row.Index] = row
          else:
            false_positive += 1
    # Loop through rows already indexed and match the fingerprint
    for row in df.loc[list(final_row_set)].itertuples(index=True, name="Row"):
      for i in range(len(row.title)):
        if row.title[i:].startswith(substr):
          result[row.Index] = row
    # Debug
    print("False positive ", false_positive / len(df))
    if DEBUG_RESULT:
      print(pd.DataFrame(result.values()))

# Prototype benchmarking
SUBSTRINGS = ["Doctor", "Star", "Hello", "Good", "Welcome", "Bye", "Bad"]

## Approach #1
# dfa = df.copy()
# def bench_approach_1():
#   for substr in SUBSTRINGS:
#     naive_filter(dfa, substr)
# t = timeit.timeit(bench_approach_1, number=1)
# print(f"Naive filter: {t * 1000} ms")

# ## Approach #2
# dfb = df.copy()
# def bench_approach_2():
#   dfb["suffix_array"] = dfb["title"].apply(build_suffix_array)
#   for substr in SUBSTRINGS:
#     sa_filter(dfb, substr)
# t = timeit.timeit(bench_approach_2, number=1)
# print(f"Suffix array filter: {t * 1000} ms")

# # Approach #3
# dfb = df.copy()
# def bench_approach_3():
#   dfb["fp"] = dfb["title"].apply(build_fp)
#   for substr in SUBSTRINGS:
#     fp_filter(dfb, substr)
# t = timeit.timeit(bench_approach_3, number=1)
# print(f"Fingerprint filter: {t * 1000} ms")

# dfb = df.copy()
# def bench_approach_3b():
#   dfb["fp"] = dfb["title"].apply(build_1b_fp)
#   for substr in SUBSTRINGS:
#     fp_filter(dfb, substr, False)
# t = timeit.timeit(bench_approach_3b, number=1)
# print(f"1B-Fingerprint filter: {t * 1000} ms")

## Approach #4
# for as_alphabet_size in [16, 64, 128, 256]:
#   tqdm.pandas(desc="Preprocessing signature: Alphabet size {}".format(as_alphabet_size))
#   dfb = df.copy()
#   def bench_approach_4():
#     sign_hashtable = {}
#     dfb.progress_apply(preprocess_as, axis=1, args=(sign_hashtable, as_alphabet_size))
#     for substr in SUBSTRINGS:
#       inverted_index_1(dfb, sign_hashtable, as_alphabet_size, substr)
#   t = timeit.timeit(bench_approach_4, number=1)
#   print(f"Inverted index 1: {t * 1000} ms")

# Approach #5
# tqdm.pandas(desc="Preprocessing fp inverted index")
# dfb = df.copy()
# sign_hashtable = {}
# def bench_approach_5():
#   dfb.progress_apply(preprocess_fp, axis=1, args=(sign_hashtable,))
#   for substr in SUBSTRINGS:
#     inverted_index_2(dfb, sign_hashtable, substr)
# t = timeit.timeit(bench_approach_5, number=1)
# print(f"Inverted index 2: {t * 1000} ms")

# Approach #6
# dfb = df.copy()
# def bench_approach_6():
#   candidates = semi_inverted_join(dfb, SUBSTRINGS)
#   print(candidates)
#   # TODO: Real filtering
# t = timeit.timeit(bench_approach_6, number=1)
# print(f"Semi inverted join: {t * 1000} ms")

# Approach #7
# dfb = df.copy()
# t = timeit.timeit(lambda: hybrid_inverted_index(dfb, SUBSTRINGS), number=1)
# print(f"Hybrid inverted join: {t * 1000} ms")

# # Approach #8
# dfb = df.copy()
# def bench_approach_8():
#   dfb["fp"] = dfb["title"].apply(build_fp)
#   hybrid_fp_filter(dfb, SUBSTRINGS)
# t = timeit.timeit(bench_approach_8, number=1)
# print(f"Hybrid fingerprint filter: {t * 1000} ms")

# Approach #9
dfb = df.copy()
def bench_approach_9():
  dfb["fp"] = dfb["title"].apply(build_1b_fp)
  hybrid_fp_filter(dfb, SUBSTRINGS, False)
t = timeit.timeit(bench_approach_9, number=1)
print(f"Hybrid 1B-Fingerprint filter: {t * 1000} ms")
