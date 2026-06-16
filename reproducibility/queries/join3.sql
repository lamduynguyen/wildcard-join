-- Contextual Meaning: This query measures Thread Coherence or "Discussion Drift."
-- It doesn't just ask if a topic was mentioned; it asks if a top-level comment directly under a story stayed on the exact same topic as the story's headline.
-- Analytical Insights:
-- - Topic Stickiness: Some topics (like "Rust" or "PostgreSQL") might be highly technical, focused discussions.
--   Other topics (like "Google" or "OpenAI") might act as springboards where the comment section immediately devolves into unrelated discussions.
-- - Link-Only vs. Discussion Posts: If a topic appears frequently in headlines but rarely matches the comment text, it tells analysts that users are treating those posts as news bulletins rather than conversation starters.
SELECT
    t.topic_id,
    t.pattern,
    COUNT(DISTINCT s.id) AS stories_mentioning_topic,
    COUNT(DISTINCT c.id) AS on_topic_comments,
    COUNT(DISTINCT c.by) AS unique_commenters
FROM hackernews s
JOIN hackernews c ON c.parent = s.id
JOIN topics t ON s.title LIKE t.pattern AND c.text LIKE t.pattern
WHERE s.type IN ('1', '5')
  AND c.type = '2'
  AND s.deleted = '0'
  AND c.deleted = '0'
GROUP BY t.topic_id, t.pattern
HAVING COUNT(DISTINCT c.id) >= 5
ORDER BY on_topic_comments DESC;
