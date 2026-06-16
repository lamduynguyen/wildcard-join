-- Contextual Meaning: This is a "Trust and Safety" query.
-- It searches possible bad actors (spammers, affiliate marketers, or malware distributors)'s spam stories
--  that bypass the platform's automated URL filters.
-- Analytical Insights:
-- - Detect not-yet-seen attacks: This query reveals how attackers exploit the community's trust in text ubmissions.
--   If a high-scoring story appears in this result set, it means a spammer successfully engineered a text post
--   that fooled both the community (who upvoted it) and basic URL filters,
-- - Topic generating the most malicious content: Analyzing which topics appear most frequently in this query,
--   platform moderators can learn which trends are actively being weaponized.
SELECT
    t.topic_id,
    h.id AS story_id,
    h.by AS author,
    h.title,
    h.url,
    h.text,
    CAST(h.score AS INT) AS score
FROM hackernews h
JOIN topics t
  ON h.title LIKE t.pattern
WHERE h.type IN ('1', '5')
  AND h.deleted = '0'
  AND NOT EXISTS (
    SELECT 1
    FROM blocklists b
    WHERE h.text LIKE b.host_pattern
  )
ORDER BY score DESC;