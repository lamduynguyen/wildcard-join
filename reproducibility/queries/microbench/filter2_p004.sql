-- Analytical Context: An AI infrastructure researcher wants to profile how developers are
-- compiling, quantizing, and deploying open-weights models in 2025.
-- Model titles on Hacker News contain incredibly dense, regular, yet variable string layouts
-- (e.g., DeepSeek-R1-Distill-Qwen-14B-GGUF, Llama-3.3-70B-Instruct, Qwen2.5-Coder-32B).
-- Variant: 4 wildcard pattern(s)
SELECT
    CASE
        WHEN title LIKE '%DeepSeek%R_%Distill%_B%' THEN 'DeepSeek Distilled Architecture'
        WHEN title LIKE '%DeepSeek%R_%__B%' THEN 'DeepSeek Full-Scale Reasoner (671B)'
        WHEN title LIKE '%DeepSeek%V_%__B%' THEN 'DeepSeek V-Series Base Tier'
        WHEN title LIKE '%Llama_3._%_B%GGUF%' OR title LIKE '%Llama_3._%_B%_q_%' THEN 'Llama 3.x Edge Quantized (GGUF/AWQ)'
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
       OR title LIKE '%DeepSeek%R_%__B%'
       OR title LIKE '%DeepSeek%V_%__B%'
       OR title LIKE '%Llama_3._%_B%GGUF%' OR title LIKE '%Llama_3._%_B%_q_%'
  )
GROUP BY 1
ORDER BY reported_variants DESC;
