# HackerNews benchmark

Focus on Wildcard operation.
We should also use some queries from SQLStorm for benchmarking

## Dataset

- Download HackerNews dataset here: https://huggingface.co/datasets/lamduynguyen/hackernews
- Extract into `dataset` folder:
```shell
❯ ls dataset
blocklists.csv  hackernews.csv topics.csv
```
- Run SQL queries in `schema.sql` to prepare three corresponding tables
- Load those csv files in `dataset` folder with SQL queries from `load.sql`

## Benchmark queries

All SQL queries for benchmarking are in `queries` folder
