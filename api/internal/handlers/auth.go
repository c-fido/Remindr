package handlers

import (
	"encoding/json"
	"errors"
	"net/http"
	"time"

	"github.com/c-fido/remindr/api/internal/auth"
	"github.com/c-fido/remindr/api/internal/config"
	"github.com/c-fido/remindr/api/internal/validation"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
)

type AuthHandler struct {
	db  *pgxpool.Pool
	cfg config.Config
}

func NewAuthHandler(db *pgxpool.Pool, cfg config.Config) *AuthHandler {
	return &AuthHandler{db: db, cfg: cfg}
}

type authRequest struct {
	Email    string `json:"email"`
	Password string `json:"password"`
}

type refreshRequest struct {
	RefreshToken string `json:"refresh_token"`
}

func (h *AuthHandler) Register(w http.ResponseWriter, r *http.Request) {
	h.signUp(w, r, http.StatusCreated)
}

func (h *AuthHandler) Login(w http.ResponseWriter, r *http.Request) {
	var req authRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, "invalid request body")
		return
	}
	if err := validation.AuthCredentials(req.Email, req.Password); err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}

	ctx := r.Context()
	var userID, passwordHash string
	err := h.db.QueryRow(ctx,
		`SELECT id::text, password FROM users WHERE email = $1`,
		req.Email,
	).Scan(&userID, &passwordHash)
	if errors.Is(err, pgx.ErrNoRows) {
		writeError(w, http.StatusUnauthorized, "invalid email or password")
		return
	}
	if err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}
	if !auth.CheckPassword(passwordHash, req.Password) {
		writeError(w, http.StatusUnauthorized, "invalid email or password")
		return
	}

	h.issueTokens(w, r, userID)
}

func (h *AuthHandler) signUp(w http.ResponseWriter, r *http.Request, status int) {
	var req authRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, "invalid request body")
		return
	}
	if err := validation.AuthCredentials(req.Email, req.Password); err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}

	passwordHash, err := auth.HashPassword(req.Password)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "could not hash password")
		return
	}

	ctx := r.Context()
	var userID string
	err = h.db.QueryRow(ctx,
		`INSERT INTO users (email, password) VALUES ($1, $2) RETURNING id::text`,
		req.Email, passwordHash,
	).Scan(&userID)
	if err != nil {
		var pgErr *pgconn.PgError
		if errors.As(err, &pgErr) && pgErr.Code == "23505" {
			writeError(w, http.StatusConflict, "email already registered")
			return
		}
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}

	h.issueTokensWithStatus(w, r, userID, status)
}

func (h *AuthHandler) Refresh(w http.ResponseWriter, r *http.Request) {
	var req refreshRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, "invalid request body")
		return
	}
	if req.RefreshToken == "" {
		writeError(w, http.StatusBadRequest, "refresh_token is required")
		return
	}

	ctx := r.Context()
	tokenHash := auth.HashRefreshToken(req.RefreshToken)

	var userID string
	var tokenID string
	err := h.db.QueryRow(ctx,
		`SELECT id::text, user_id::text FROM refresh_tokens
		 WHERE token_hash = $1 AND expires_at > now()`,
		tokenHash,
	).Scan(&tokenID, &userID)
	if errors.Is(err, pgx.ErrNoRows) {
		writeError(w, http.StatusUnauthorized, "invalid or expired refresh token")
		return
	}
	if err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}

	// Rotate refresh token.
	if _, err := h.db.Exec(ctx, `DELETE FROM refresh_tokens WHERE id = $1`, tokenID); err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}

	h.issueTokens(w, r, userID)
}

func (h *AuthHandler) issueTokens(w http.ResponseWriter, r *http.Request, userID string) {
	h.issueTokensWithStatus(w, r, userID, http.StatusOK)
}

func (h *AuthHandler) issueTokensWithStatus(w http.ResponseWriter, r *http.Request, userID string, status int) {
	ctx := r.Context()

	accessToken, err := auth.IssueAccessToken(userID, h.cfg.JWTSecret)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "could not issue access token")
		return
	}

	plainRefresh, hashRefresh, err := auth.NewRefreshToken()
	if err != nil {
		writeError(w, http.StatusInternalServerError, "could not issue refresh token")
		return
	}

	expiresAt := time.Now().UTC().Add(auth.RefreshTokenTTL)
	_, err = h.db.Exec(ctx,
		`INSERT INTO refresh_tokens (user_id, token_hash, expires_at) VALUES ($1, $2, $3)`,
		userID, hashRefresh, expiresAt,
	)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}

	writeJSON(w, status, auth.TokenPair{
		AccessToken:  accessToken,
		RefreshToken: plainRefresh,
		ExpiresIn:    auth.ExpiresIn,
	})
}
