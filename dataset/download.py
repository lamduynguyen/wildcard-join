from datasets import load_dataset

review_dataset = load_dataset(
  "McAuley-Lab/Amazon-Reviews-2023",
  "raw_review_Electronics",
  split="full",
  trust_remote_code=True)
item_dataset = load_dataset(
  "McAuley-Lab/Amazon-Reviews-2023",
  "raw_meta_Electronics",
  split="full",
  trust_remote_code=True)

review_dataset.to_parquet("./review_Electronics.parquet")
item_dataset.to_parquet("./item_Electronics.parquet")
