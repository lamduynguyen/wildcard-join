# Prototype impl for Wildcard Join

This is the wildcard join prototype, proposed in the paper "Teach Your Database to LIKE Strings via Efficient Wildcard Joins
and Filters" -- currently under submission for VLDB 2027.

Repo structure:
- `aho_corasick`: Containing the wildcard join prototype
- `benchmark` & `dataset`: Simple benchmark and dataset. Note that real dataset is [downloaded from here](https://huggingface.co/datasets/lamduynguyen/hackernews)
- `test`: The testing suit for wildcard join algorithm
- `third_party`: Dependencies
