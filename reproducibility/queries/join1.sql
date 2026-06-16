-- Contextual Meaning: This query acts as a "Zeitgeist" monitor.
-- By explicitly filtering for stories with a score strictly greater than 20,
--  it ignores the low-effort stories ("noise") and focuses solely on what the community values ("signal").
-- Analytical Insights:
-- - Quality vs. Virality: By aggregating both total score (upvotes) and total comments (descendants),
--   an analyst can identify topics that are controversial vs. universally liked.
-- - Community Interest Profiling: This tells product managers exactly which technologies, companies, or concepts are currently driving the engagement metrics.
SELECT
    t.topic_id,
    t.pattern,
    COUNT(DISTINCT h.id) AS story_count,
    SUM(CAST(h.score AS INT)) AS total_score,
    SUM(CAST(h.descendants AS INT)) AS total_comments,
    AVG(CAST(h.score AS INT)) AS avg_score
FROM hackernews h
JOIN topics t
  ON h.title LIKE t.pattern
WHERE h.type IN ('1', '5')
  AND h.deleted = '0'
  AND CAST(h.score AS INT) > 20
GROUP BY t.topic_id, t.pattern
HAVING COUNT(DISTINCT h.id) >= 10
ORDER BY total_score DESC, total_comments DESC;