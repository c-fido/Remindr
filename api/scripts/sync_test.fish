#!/usr/bin/env fish
# Usage: ./scripts/sync_test.fish YOUR_ACCESS_TOKEN [API_BASE]
# Example: ./scripts/sync_test.fish (curl -s ... | jq -r .access_token)

if test (count $argv) -lt 1
    echo "usage: sync_test.fish ACCESS_TOKEN [API_BASE]"
    exit 1
end

set -l token $argv[1]
set -l base "http://localhost:8080"
if test (count $argv) -ge 2
    set base $argv[2]
end

set -l now (date +%s)
set -l fire_at (math $now + 3600)
set -l id (uuidgen | tr '[:upper:]' '[:lower:]')

set -l body (printf '{"device_id":"550e8400-e29b-41d4-a716-446655440001","last_sync_at":0,"changes":[{"id":"%s","message":"sync test","fire_at":%s,"recurrence":"none","fired":false,"deleted":false,"updated_at":%s}]}' $id $fire_at $now)

curl -s -X POST "$base/v1/sync" \
    -H "Authorization: Bearer $token" \
    -H 'Content-Type: application/json' \
    -d $body
echo
