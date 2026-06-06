#!/bin/bash
cd "$(dirname "$0")/.."
DB_PATH="harmony_v2.db"
if [ ! -f "$DB_PATH" ]; then
    echo "Database not found at $DB_PATH"
    exit 1
fi

echo "--- Album Consolidated View Data ---"
sqlite3 "$DB_PATH" "SELECT al.name, COUNT(DISTINCT al.artist_id), MAX(ar.name) FROM albums al LEFT JOIN artists ar ON al.artist_id = ar.id GROUP BY al.name HAVING COUNT(DISTINCT al.artist_id) > 1 LIMIT 20;"

echo "--- Problematic Album Details (Sample) ---"
PROBLEM_ALBUM=$(sqlite3 "$DB_PATH" "SELECT al.name FROM albums al GROUP BY al.name HAVING COUNT(DISTINCT al.artist_id) > 1 LIMIT 1;")
if [ -n "$PROBLEM_ALBUM" ]; then
    echo "Inspecting album: $PROBLEM_ALBUM"
    sqlite3 "$DB_PATH" "SELECT al.id, al.name, al.artist_id, ar.name FROM albums al LEFT JOIN artists ar ON al.artist_id = ar.id WHERE al.name = '$PROBLEM_ALBUM';"
fi
