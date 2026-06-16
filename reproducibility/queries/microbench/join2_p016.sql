-- Analytical Context: An AI infrastructure researcher wants to profile how developers are
-- compiling, quantizing, and deploying open-weights models in 2025.
-- Model titles on Hacker News contain incredibly dense, regular, yet variable string layouts
-- (e.g., DeepSeek-R1-Distill-Qwen-14B-GGUF, Llama-3.3-70B-Instruct, Qwen2.5-Coder-32B).
-- Variant: 16 wildcard pattern(s) — EXISTS/JOIN rewrite
WITH patterns (priority, pattern, model_deployment_profile) AS (
    VALUES
        ( 1, '%DeepSeek%R_%Distill%_B%', 'DeepSeek Distilled Architecture'),
        ( 2, '%DeepSeek%R_%__B%',        'DeepSeek Full-Scale Reasoner (671B)'),
        ( 3, '%DeepSeek%V_%__B%',        'DeepSeek V-Series Base Tier'),
        ( 4, '%Llama_3._%_B%GGUF%',      'Llama 3.x Edge Quantized (GGUF/AWQ)'),
        ( 4, '%Llama_3._%_B%_q_%',       'Llama 3.x Edge Quantized (GGUF/AWQ)'),
        ( 5, '%Llama_3._%_B%Instruct%',  'Llama 3.x Production Instruct'),
        ( 6, '%Llama_4_%_B%Instruct%',   'Llama 4 Next-Gen Inline'),
        ( 7, '%Qwen_2._%Coder%_B%',      'Qwen 2.5 Coder Specialized'),
        ( 8, '%Qwen_2._%_B%Instruct%',   'Qwen 2.5 Core Instruct'),
        ( 9, '%Mistral%7B%Instruct%',    'Mistral 7B Instruct Variants'),
        (10, '%Mixtral%_x%B%Instruct%',  'Mistral Mixtral MoE Architecture'),
        (11, '%Phi-_% %B%Instruct%',     'Phi-3 / Phi-4 Microsoft Edge Models'),
        (12, '%Gemma%_B%Instruct%',      'Gemma 2 / 3 Google Open Models'),
        (13, '%Command-R%_B%',           'Command-R Cohere Production'),
        (14, '%Yi-__%B%Chat%',           'Yi Large Series (01.AI)'),
        (15, '%Falcon%_B%Instruct%',     'Falcon RefinedWeb Models'),
        (16, '%MPT%_B%Instruct%',        'MPT MosaicML Architecture')
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
