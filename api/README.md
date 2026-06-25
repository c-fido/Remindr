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

# Delete (replace ID)
curl -s -X DELETE http://localhost:8080/v1/reminders/<id> -H "Authorization: Bearer $TOKEN"
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
| POST | `/v1/sync` | JWT | Day 4 |
