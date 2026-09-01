---
configs:
  - config_name: hackernews
    default: true
    data_files:
      - split: train
        path: hackernews.csv
  - config_name: blocklists
    data_files:
      - split: train
        path: blocklists.csv
  - config_name: topics
    data_files:
      - split: train
        path: topics.csv
---

# HackerNews wildcard join and filter workload

The evaluation data for "Teach Your Database to LIKE Strings via Efficient
Wildcard Joins and Filters". Three CSV files, three different schemas, so they
are three configurations rather than three shards of one table.

## Configurations

`hackernews` is the probe side, 3,886,492 rows of HackerNews items.

| column | type |
| - | - |
| `id` | int |
| `deleted` | int |
| `type` | int |
| `by` | string |
| `time` | timestamp, `YYYY-MM-DD HH:MM:SS` |
| `text` | string, may contain newlines and HTML |
| `dead` | int |
| `parent` | int |
| `poll` | int |
| `url` | string |
| `score` | int |
| `title` | string |
| `descendants` | int |

`blocklists` is the build side for the join workloads, 1,000 rows of `LIKE`
patterns over hostnames.

| column | type |
| - | - |
| `block_id` | int |
| `host_pattern` | string, a `LIKE` pattern, e.g. `%//___vegas___.com%` |
| `category` | string |

`topics` is the build side for the filter workloads, 1,000 rows.

| column | type |
| - | - |
| `topic_id` | int |
| `pattern` | string, a `LIKE` pattern, e.g. `%rust%` |

## Loading

```python
from datasets import load_dataset

hn     = load_dataset("lamduynguyen/hackernews", "hackernews", split="train")
blocks = load_dataset("lamduynguyen/hackernews", "blocklists", split="train")
topics = load_dataset("lamduynguyen/hackernews", "topics", split="train")
```

Pin a revision if you are reproducing published numbers. The main branch moves;
a bare repository name does not identify the data anyone else measured.

```python
REV = "87f6bf9adb6e6fb41e7591a0d83787487e7e7fb3"
hn = load_dataset("lamduynguyen/hackernews", "hackernews", revision=REV, split="train")
```

`text` has embedded newlines inside quoted fields, so `wc -l` does not count
rows in `hackernews.csv`. Anything reading it has to parse quoting properly.

## Checksums

sha256, at revision `87f6bf9adb6e6fb41e7591a0d83787487e7e7fb3`:

```
c371f8abde30a066dddf36f85bf6d906a90d1cf2f555f4c08560411ada32fead  hackernews.csv
ce088fe82f9c15fcd9c539af476679b903d1f53f9a17eb1479175f0fb5d66a37  blocklists.csv
04386e3e69a6c846e87aa7f14f9f55a0496cadd8d3f77f7617ae75108c8f57be  topics.csv
```

The artifact repository fetches and verifies these in
`reproducibility/fetch_dataset.sh`.
