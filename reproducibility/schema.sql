CREATE TABLE hackernews (
  id          INTEGER,
  deleted     SMALLINT,
  type        SMALLINT,
  "by"        TEXT,
  time        TIMESTAMP,
  text        TEXT,
  dead        SMALLINT,
  parent      INTEGER,
  poll        INTEGER,
  url         TEXT,
  score       INTEGER,
  title       TEXT,
  descendants INTEGER,
  PRIMARY KEY (id)
);

CREATE TABLE blocklists (
  block_id     INTEGER,
  host_pattern VARCHAR,
  category     VARCHAR
);

CREATE TABLE topics (
  topic_id INTEGER,
  pattern  VARCHAR
);
