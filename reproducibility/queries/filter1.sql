-- The Core Objective
-- A Developer Relations (DevRel) team wants to analyze and compare Hacker News community engagement across different generations of OpenAI's GPT models (specifically isolating the newer GPT-4o / Omni against older iterations like GPT-4 Base, GPT-3.5, and GPT-3).
-- The Problem It Solves
-- Upvotes (scores) on Hacker News can often be a vanity metric; a story might get heavily upvoted due to hype or a catchy headline without actually sparking deep technical discussion.
-- This query filters out the "noise" to distinguish between highly upvoted but ignored stories and stories that genuinely spark active conversation.
WITH gpt_4o_stories AS (
    SELECT id, 'OpenAI GPT-4o (Omni)' AS model_iteration, score, descendants
    FROM hackernews
    WHERE type = 1 AND deleted = 0 AND dead = 0
      AND (title ILIKE '%gpt_4_o%' OR title ILIKE '%gpt4_o%' OR title ILIKE '%gpt4o%')
),
older_gpt_stories AS (
    SELECT id,
        CASE
            WHEN title ILIKE '%gpt_4%'   OR title ILIKE '%gpt4%'   THEN 'OpenAI GPT-4 Base/Turbo'
            WHEN title ILIKE '%gpt_3._%' OR title ILIKE '%gpt3._%' THEN 'OpenAI GPT-3.5 Legacy'
            ELSE 'OpenAI GPT-3 Original'
        END AS model_iteration,
        score, descendants
    FROM hackernews
    WHERE type = 1 AND deleted = 0 AND dead = 0
      AND title ILIKE '%gpt%'
      AND NOT (title ILIKE '%gpt_4_o%' OR title ILIKE '%gpt4_o%' OR title ILIKE '%gpt4o%')
),
gpt_stories AS (
    SELECT * FROM gpt_4o_stories
    UNION ALL
    SELECT * FROM older_gpt_stories
),
story_comments AS (
    SELECT
        s.model_iteration,
        s.score                                AS story_score,
        s.descendants                          AS story_descendants,
        COUNT(c.id)                            AS direct_comment_count,
        AVG(c.score)                           AS avg_comment_score
    FROM gpt_stories s
    JOIN hackernews c ON c.parent = s.id
    WHERE c.deleted = 0 AND c.dead = 0
    GROUP BY s.id, s.model_iteration, s.score, s.descendants
)
SELECT
    model_iteration,
    COUNT(*)                                                          AS story_count,
    SUM(story_score)                                                  AS total_upvotes,
    SUM(story_descendants)                                            AS total_comments,
    ROUND(AVG(story_score),         1)                                AS avg_story_score,
    ROUND(AVG(direct_comment_count),1)                                AS avg_direct_comments,
    ROUND(AVG(avg_comment_score),   1)                                AS avg_comment_score,
    ROUND(100.0 * SUM(story_score) / SUM(SUM(story_score)) OVER (), 1) AS pct_of_upvotes,
    ROUND(100.0 * COUNT(*)         / SUM(COUNT(*))         OVER (), 1) AS pct_of_stories
FROM story_comments
GROUP BY model_iteration
ORDER BY total_upvotes DESC;