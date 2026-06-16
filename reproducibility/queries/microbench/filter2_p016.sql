-- Analytical Context: An AI infrastructure researcher wants to profile how developers are
-- compiling, quantizing, and deploying open-weights models in 2025.
-- Model titles on Hacker News contain incredibly dense, regular, yet variable string layouts
-- (e.g., DeepSeek-R1-Distill-Qwen-14B-GGUF, Llama-3.3-70B-Instruct, Qwen2.5-Coder-32B).
-- Variant: 16 wildcard pattern(s)
SELECT
    CASE
        WHEN title LIKE '%DeepSeek%R_%Distill%_B%' THEN 'DeepSeek Distilled Architecture'
        WHEN title LIKE '%DeepSeek%R_%__B%' THEN 'DeepSeek Full-Scale Reasoner (671B)'
        WHEN title LIKE '%DeepSeek%V_%__B%' THEN 'DeepSeek V-Series Base Tier'
        WHEN title LIKE '%Llama_3._%_B%GGUF%' OR title LIKE '%Llama_3._%_B%_q_%' THEN 'Llama 3.x Edge Quantized (GGUF/AWQ)'
        WHEN title LIKE '%Llama_3._%_B%Instruct%' THEN 'Llama 3.x Production Instruct'
        WHEN title LIKE '%Llama_4_%_B%Instruct%' THEN 'Llama 4 Next-Gen Inline'
        WHEN title LIKE '%Qwen_2._%Coder%_B%' THEN 'Qwen 2.5 Coder Specialized'
        WHEN title LIKE '%Qwen_2._%_B%Instruct%' THEN 'Qwen 2.5 Core Instruct'
        WHEN title LIKE '%Mistral%7B%Instruct%' THEN 'Mistral 7B Instruct Variants'
        WHEN title LIKE '%Mixtral%_x%B%Instruct%' THEN 'Mistral Mixtral MoE Architecture'
        WHEN title LIKE '%Phi-_% %B%Instruct%' THEN 'Phi-3 / Phi-4 Microsoft Edge Models'
        WHEN title LIKE '%Gemma%_B%Instruct%' THEN 'Gemma 2 / 3 Google Open Models'
        WHEN title LIKE '%Command-R%_B%' THEN 'Command-R Cohere Production'
        WHEN title LIKE '%Yi-__%B%Chat%' THEN 'Yi Large Series (01.AI)'
        WHEN title LIKE '%Falcon%_B%Instruct%' THEN 'Falcon RefinedWeb Models'
        WHEN title LIKE '%MPT%_B%Instruct%' THEN 'MPT MosaicML Architecture'
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
       OR title LIKE '%Llama_3._%_B%Instruct%'
       OR title LIKE '%Llama_4_%_B%Instruct%'
       OR title LIKE '%Qwen_2._%Coder%_B%'
       OR title LIKE '%Qwen_2._%_B%Instruct%'
       OR title LIKE '%Mistral%7B%Instruct%'
       OR title LIKE '%Mixtral%_x%B%Instruct%'
       OR title LIKE '%Phi-_% %B%Instruct%'
       OR title LIKE '%Gemma%_B%Instruct%'
       OR title LIKE '%Command-R%_B%'
       OR title LIKE '%Yi-__%B%Chat%'
       OR title LIKE '%Falcon%_B%Instruct%'
       OR title LIKE '%MPT%_B%Instruct%'
  )
GROUP BY 1
ORDER BY reported_variants DESC;
