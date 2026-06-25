CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE users (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    email      TEXT NOT NULL UNIQUE,
    password   TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE reminders (
    id          UUID PRIMARY KEY,
    user_id     UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    message     TEXT NOT NULL,
    fire_at     BIGINT NOT NULL,
    recurrence  TEXT NOT NULL DEFAULT 'none' CHECK (recurrence IN ('none', 'daily', 'weekly')),
    fired       BOOLEAN NOT NULL DEFAULT false,
    deleted     BOOLEAN NOT NULL DEFAULT false,
    updated_at  BIGINT NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_reminders_user_updated ON reminders(user_id, updated_at);
CREATE INDEX idx_reminders_user_fire ON reminders(user_id, fire_at) WHERE deleted = false;

CREATE TABLE refresh_tokens (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id    UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL UNIQUE,
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
