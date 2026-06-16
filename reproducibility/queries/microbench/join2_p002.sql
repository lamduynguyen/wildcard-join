-- Analytical Context: An AI infrastructure researcher wants to profile how developers are
-- compiling, quantizing, and deploying open-weights models in 2025.
-- Model titles on Hacker News contain incredibly dense, regular, yet variable string layouts
-- (e.g., DeepSeek-R1-Distill-Qwen-14B-GGUF, Llama-3.3-70B-Instruct, Qwen2.5-Coder-32B).
-- Variant: 2 wildcard pattern(s) — EXISTS/JOIN rewrite
WITH patterns (priority, pattern, model_deployment_profile) AS (
    VALUES
        (1, '%DeepSeek%R_%Distill%_B%', 'DeepSeek Distilled Architecture'),
        (2, '%DeepSeek%R_%__B%',        'DeepSeek Full-Scale Reasoner (671B)')
)
SELECT
    (
        SELECT p.model_deployment_profile
        FROM patterns p
        WHERE h.title LIKE p.pattern
        ORDER BY p.priority
        LIMIT 1
    ) AS model_deployment_profile,
    COUNT(*)                      AS reported_variants,
    SUM(CAST(score       AS INT)) AS aggregate_upvotes,
    AVG(CAST(descendants AS INT)) AS conversation_depth
FROM hackernews h
WHERE type IN ('1', '5')
  AND deleted = '0'
  AND EXISTS (
        SELECT 1 FROM patterns p WHERE h.title LIKE p.pattern
      )
GROUP BY 1
ORDER BY reported_variants DESC;
