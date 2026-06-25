# Remindr API

Go REST API for Remindr Sync — auth, reminder CRUD, and daemon sync.

## Prerequisites

- Go 1.22+
- PostgreSQL (local or [Neon](https://neon.tech))

## Setup

```bash
cp .env.example .env   # fill DATABASE_URL and JWT_SECRET
```

Run migrations:

```bash
# install: go install -tags 'postgres' github.com/golang-migrate/migrate/v4/cmd/migrate@latest
migrate -path migrations -database "$DATABASE_URL" up
```

## Run

```bash
go run ./cmd/server
```

Health check: `curl http://localhost:8080/health`

### Quick test (curl)

**bash/zsh:**

```bash
# Register
curl -s -X POST http://localhost:8080/v1/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"email":"you@example.com","password":"password123"}'

# Login (save access_token from response)
TOKEN=$(curl -s -X POST http://localhost:8080/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"you@example.com","password":"password123"}' | jq -r .access_token)

# Create reminder (fire_at = unix seconds)
curl -s -X POST http://localhost:8080/v1/reminders \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"message":"standup","fire_at":'$(date -v+1H +%s)',"recurrence":"daily"}'

# List
curl -s http://localhost:8080/v1/reminders -H "Authorization: Bearer $TOKEN"

# Sync
NOW=$(date +%s)
curl -s -X POST http://localhost:8080/v1/sync \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d "{\"device_id\":\"550e8400-e29b-41d4-a716-446655440001\",\"last_sync_at\":0,\"changes\":[{\"id\":\"$(uuidgen | tr '[:upper:]' '[:lower:]')\",\"message\":\"sync test\",\"fire_at\":$((NOW + 3600)),\"recurrence\":\"none\",\"fired\":false,\"deleted\":false,\"updated_at\":$NOW}]}"
```

**fish** — don't use bash `$((...))` syntax; use `printf` or the helper script:

```fish
set TOKEN (curl -s -X POST http://localhost:8080/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"you@example.com","password":"password123"}' | jq -r .access_token)

chmod +x scripts/sync_test.fish
./scripts/sync_test.fish $TOKEN
```

Or manually:

```fish
set NOW (date +%s)
set FIRE_AT (math $NOW + 3600)
set ID (uuidgen | tr '[:upper:]' '[:lower:]')

curl -s -X POST http://localhost:8080/v1/sync \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d (printf '{"device_id":"550e8400-e29b-41d4-a716-446655440001","last_sync_at":0,"changes":[{"id":"%s","message":"sync test","fire_at":%s,"recurrence":"none","fired":false,"deleted":false,"updated_at":%s}]}' $ID $FIRE_AT $NOW)
```

## Routes (v1)

| Method | Path | Auth | Status |
|--------|------|------|--------|
| GET | `/health` | — | ✅ |
| POST | `/v1/auth/register` | — | ✅ |
| POST | `/v1/auth/login` | — | ✅ |
| POST | `/v1/auth/refresh` | — | ✅ |
| GET | `/v1/reminders` | JWT | ✅ |
| POST | `/v1/reminders` | JWT | ✅ |
| DELETE | `/v1/reminders/{id}` | JWT | ✅ |
| POST | `/v1/sync` | JWT | ✅ |
