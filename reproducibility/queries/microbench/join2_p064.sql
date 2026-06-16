-- Analytical Context: An AI infrastructure researcher wants to profile how developers are
-- compiling, quantizing, and deploying open-weights models in 2025.
-- Model titles on Hacker News contain incredibly dense, regular, yet variable string layouts
-- (e.g., DeepSeek-R1-Distill-Qwen-14B-GGUF, Llama-3.3-70B-Instruct, Qwen2.5-Coder-32B).
-- Variant: 64 wildcard pattern(s) — EXISTS/JOIN rewrite
WITH patterns (priority, pattern, model_deployment_profile) AS (
    VALUES
        ( 1, '%DeepSeek%R_%Distill%_B%',       'DeepSeek Distilled Architecture'),
        ( 2, '%DeepSeek%R_%__B%',              'DeepSeek Full-Scale Reasoner (671B)'),
        ( 3, '%DeepSeek%V_%__B%',              'DeepSeek V-Series Base Tier'),
        ( 4, '%Llama_3._%_B%GGUF%',            'Llama 3.x Edge Quantized (GGUF/AWQ)'),
        ( 4, '%Llama_3._%_B%_q_%',             'Llama 3.x Edge Quantized (GGUF/AWQ)'),
        ( 5, '%Llama_3._%_B%Instruct%',        'Llama 3.x Production Instruct'),
        ( 6, '%Llama_4_%_B%Instruct%',         'Llama 4 Next-Gen Inline'),
        ( 7, '%Qwen_2._%Coder%_B%',            'Qwen 2.5 Coder Specialized'),
        ( 8, '%Qwen_2._%_B%Instruct%',         'Qwen 2.5 Core Instruct'),
        ( 9, '%Mistral%7B%Instruct%',          'Mistral 7B Instruct Variants'),
        (10, '%Mixtral%_x%B%Instruct%',        'Mistral Mixtral MoE Architecture'),
        (11, '%Phi-_% %B%Instruct%',           'Phi-3 / Phi-4 Microsoft Edge Models'),
        (12, '%Gemma%_B%Instruct%',            'Gemma 2 / 3 Google Open Models'),
        (13, '%Command-R%_B%',                 'Command-R Cohere Production'),
        (14, '%Yi-__%B%Chat%',                 'Yi Large Series (01.AI)'),
        (15, '%Falcon%_B%Instruct%',           'Falcon RefinedWeb Models'),
        (16, '%MPT%_B%Instruct%',              'MPT MosaicML Architecture'),
        (17, '%StarCoder%_B%',                 'StarCoder Code-Specialised'),
        (18, '%CodeLlama%_B%Instruct%',        'CodeLlama Code Instruct'),
        (19, '%vicuna%__%b%',                  'Vicuna LMSYS Fine-Tune'),
        (20, '%WizardLM%_B%',                  'WizardLM Evol-Instruct Series'),
        (21, '%Orca%__%B%',                    'Orca / Orca-2 Microsoft Research'),
        (22, '%alpaca%__%b%',                  'Alpaca Stanford Fine-Tune'),
        (23, '%OpenHermes%_B%',                'OpenHermes Teknium Series'),
        (24, '%neural-chat%__%b%',             'Neural-Chat Intel Optimised'),
        (25, '%Zephyr%_B%',                    'Zephyr Beta / Gemma Alignment'),
        (26, '%SOLAR%__%B%',                   'SOLAR Upstage 10.7B'),
        (27, '%Nous-Hermes%_B%',               'Nous-Hermes Fine-Tune Series'),
        (28, '%BioMed%_B%',                    'BioMedLM / BioMistral Domain'),
        (29, '%Meditron%__%B%',                'Meditron Medical Domain'),
        (30, '%Platypus%_B%',                  'Platypus Reasoning Fine-Tune'),
        (31, '%dolly%__%b%',                   'Dolly Databricks Instruct'),
        (32, '%StableLM%_B%',                  'StableLM Stability AI'),
        (33, '%RedPajama%_B%',                 'RedPajama Together AI'),
        (34, '%OpenLLaMA%_B%',                 'OpenLLaMA Open Reproduction'),
        (35, '%TinyLlama%_B%',                 'TinyLlama Compact Edge'),
        (36, '%phi-2%',                        'Phi-2 Small Language Model'),
        (37, '%Mistral-NeMo%_B%',              'Mistral NeMo Nvidia Collab'),
        (38, '%Qwen1.5%_B%Instruct%',          'Qwen 1.5 Legacy Instruct'),
        (39, '%InternLM%__%B%',                'InternLM Shanghai AI Lab'),
        (40, '%Baichuan%__%B%',                'Baichuan Chinese LLM'),
        (41, '%ChatGLM%__%B%',                 'ChatGLM Tsinghua Series'),
        (42, '%Aquila%__%B%',                  'Aquila BAAI Chinese Model'),
        (43, '%bloom%__%b%',                   'BLOOM BigScience Multilingual'),
        (44, '%OPT%__%B%',                     'OPT Meta Legacy Open'),
        (45, '%GPT-J%_B%',                     'GPT-J / GPT-NeoX EleutherAI'),
        (46, '%pythia%__%b%',                  'Pythia EleutherAI Scaling'),
        (47, '%CausalLM%_B%',                  'CausalLM Generic Base'),
        (48, '%DeepSeek%Coder%V_%_B%Instruct%','DeepSeek-Coder V2 Instruct'),
        (49, '%Llama_3._%_B%Q4%',              'Llama 3.x GGUF Q4 Quant'),
        (50, '%Llama_3._%_B%Q8%',              'Llama 3.x GGUF Q8 Quant'),
        (51, '%Qwen_2._%Math%_B%',             'Qwen 2.5 Math Specialised'),
        (52, '%Qwen_2._%VL%_B%',               'Qwen 2.5 VL Vision-Language'),
        (53, '%MiniCPM%_B%',                   'MiniCPM Compact CPM Series'),
        (54, '%SmolLM%_B%',                    'SmolLM HuggingFace Tiny'),
        (55, '%Jamba%_B%',                     'Jamba Mamba Hybrid SSM'),
        (56, '%RWKV%__%B%',                    'RWKV RNN-LM Series'),
        (57, '%mamba%__%b%',                   'Mamba State Space Model'),
        (58, '%granite%__%b%instruct%',        'Granite IBM Instruct'),
        (59, '%Mistral-Large%_B%',             'Mistral Large / Medium Tier'),
        (60, '%DeepSeek%V__%_B%',              'DeepSeek-V3 / V4 Latest'),
        (61, '%Llama_3.3%_B%',                 'Llama 3.3 Specific Release'),
        (62, '%Qwen3%_B%',                     'Qwen3 Next-Generation Series'),
        (63, '%Gemma_3%_B%Instruct%',          'Gemma 3 Latest Google Release'),
        (64, '%Llama_4%Scout%',                'Llama 4 Scout / Maverick'),
        (64, '%Llama_4%Maverick%',             'Llama 4 Scout / Maverick')
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
