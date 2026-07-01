#!/usr/bin/env bash
# Smoke test against a running API. Usage:
#   ./scripts/smoke_test.sh [BASE_URL]
# Requires: curl, jq

set -euo pipefail

BASE="${1:-http://localhost:8080}"
EMAIL="smoke-$(date +%s)@example.com"
PASS="password123"

echo "== health =="
curl -sf "$BASE/health" | jq .

echo "== register =="
REG=$(curl -sf -X POST "$BASE/v1/auth/register" \
  -H 'Content-Type: application/json' \
  -d "{\"email\":\"$EMAIL\",\"password\":\"$PASS\"}")
echo "$REG" | jq .
TOKEN=$(echo "$REG" | jq -r .access_token)

echo "== create reminder =="
CREATE=$(curl -sf -X POST "$BASE/v1/reminders" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d "{\"message\":\"smoke test\",\"fire_at\":$(($(date +%s) + 3600)),\"recurrence\":\"none\"}")
echo "$CREATE" | jq .
ID=$(echo "$CREATE" | jq -r .id)

echo "== list =="
curl -sf "$BASE/v1/reminders" -H "Authorization: Bearer $TOKEN" | jq .

echo "== sync =="
curl -sf -X POST "$BASE/v1/sync" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d "{\"device_id\":\"550e8400-e29b-41d4-a716-446655440001\",\"last_sync_at\":0,\"changes\":[]}" | jq .

echo "== delete =="
curl -sf -X DELETE "$BASE/v1/reminders/$ID" -H "Authorization: Bearer $TOKEN"
echo "(204 expected)"

echo "OK"
