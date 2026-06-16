copy hackernews FROM 'hackernews.csv' WITH (
  FORMAT csv, HEADER TRUE, QUOTE '"', ESCAPE '"', DELIMITER ',');
copy blocklists FROM 'blocklists.csv' WITH (
  FORMAT csv, HEADER TRUE, QUOTE '"', ESCAPE '"', DELIMITER ',');
copy topics FROM 'topics.csv' WITH (
  FORMAT csv, HEADER TRUE, QUOTE '"', ESCAPE '"', DELIMITER ',');