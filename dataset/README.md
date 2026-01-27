# E-commerce dataset

**Requirement**:
```shell
pip install datasets==3.6.0
pip install pandas numpy
curl -fsSL https://ollama.com/install.sh | sh
ollama pull gemma3:1b
```

## Steps to generate the dataset:

- We use the subset `Electronics` of the [Amazon review data](https://huggingface.co/datasets/McAuley-Lab/Amazon-Reviews-2023).
  To retrieve the data, run `python download.py` to generate two parquet files: `review_Electronics.parquet` and `item_Electronics.parquet`
- Run `python generator.py` to generate two other tables -- `Campaign` and `TrendingTerm` -- resulting in two files: `campaign.csv` and `trending_term.csv`
