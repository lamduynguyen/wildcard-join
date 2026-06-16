-- Analytical Context: An AI infrastructure researcher wants to profile how developers are
-- compiling, quantizing, and deploying open-weights models in 2025.
-- Model titles on Hacker News contain incredibly dense, regular, yet variable string layouts
-- (e.g., DeepSeek-R1-Distill-Qwen-14B-GGUF, Llama-3.3-70B-Instruct, Qwen2.5-Coder-32B).
-- Variant: 32 wildcard pattern(s)
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
        WHEN title LIKE '%StarCoder%_B%' THEN 'StarCoder Code-Specialised'
        WHEN title LIKE '%CodeLlama%_B%Instruct%' THEN 'CodeLlama Code Instruct'
        WHEN title LIKE '%vicuna%__%b%' THEN 'Vicuna LMSYS Fine-Tune'
        WHEN title LIKE '%WizardLM%_B%' THEN 'WizardLM Evol-Instruct Series'
        WHEN title LIKE '%Orca%__%B%' THEN 'Orca / Orca-2 Microsoft Research'
        WHEN title LIKE '%alpaca%__%b%' THEN 'Alpaca Stanford Fine-Tune'
        WHEN title LIKE '%OpenHermes%_B%' THEN 'OpenHermes Teknium Series'
        WHEN title LIKE '%neural-chat%__%b%' THEN 'Neural-Chat Intel Optimised'
        WHEN title LIKE '%Zephyr%_B%' THEN 'Zephyr Beta / Gemma Alignment'
        WHEN title LIKE '%SOLAR%__%B%' THEN 'SOLAR Upstage 10.7B'
        WHEN title LIKE '%Nous-Hermes%_B%' THEN 'Nous-Hermes Fine-Tune Series'
        WHEN title LIKE '%BioMed%_B%' THEN 'BioMedLM / BioMistral Domain'
        WHEN title LIKE '%Meditron%__%B%' THEN 'Meditron Medical Domain'
        WHEN title LIKE '%Platypus%_B%' THEN 'Platypus Reasoning Fine-Tune'
        WHEN title LIKE '%dolly%__%b%' THEN 'Dolly Databricks Instruct'
        WHEN title LIKE '%StableLM%_B%' THEN 'StableLM Stability AI'
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
       OR title LIKE '%StarCoder%_B%'
       OR title LIKE '%CodeLlama%_B%Instruct%'
       OR title LIKE '%vicuna%__%b%'
       OR title LIKE '%WizardLM%_B%'
       OR title LIKE '%Orca%__%B%'
       OR title LIKE '%alpaca%__%b%'
       OR title LIKE '%OpenHermes%_B%'
       OR title LIKE '%neural-chat%__%b%'
       OR title LIKE '%Zephyr%_B%'
       OR title LIKE '%SOLAR%__%B%'
       OR title LIKE '%Nous-Hermes%_B%'
       OR title LIKE '%BioMed%_B%'
       OR title LIKE '%Meditron%__%B%'
       OR title LIKE '%Platypus%_B%'
       OR title LIKE '%dolly%__%b%'
       OR title LIKE '%StableLM%_B%'
  )
GROUP BY 1
ORDER BY reported_variants DESC;
