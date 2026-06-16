-- Analytical Context: An AI infrastructure researcher wants to profile how developers are
-- compiling, quantizing, and deploying open-weights models in 2025.
-- Model titles on Hacker News contain incredibly dense, regular, yet variable string layouts
-- (e.g., DeepSeek-R1-Distill-Qwen-14B-GGUF, Llama-3.3-70B-Instruct, Qwen2.5-Coder-32B).
-- Variant: 1 wildcard pattern(s)
SELECT
    CASE
        WHEN title LIKE '%DeepSeek%R_%Distill%_B%' THEN 'DeepSeek Distilled Architecture'
        ELSE 'Other 2025 AI Architecture Spec'
    END AS model_deployment_profile,
    COUNT(*) AS reported_variants,
    SUM(CAST(score AS INT)) AS aggregate_upvotes,
    AVG(CAST(descendants AS INT)) AS conversation_depth
FROM hackernews
WHERE type IN ('1', '5')
  AND deleted = '0'
  AND (
       title LIKE '%DeepSeek%R_%Distill%_B%'
  )
GROUP BY 1
ORDER BY reported_variants DESC;
