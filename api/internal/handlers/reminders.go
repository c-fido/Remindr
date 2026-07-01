package handlers

import (
	"encoding/json"
	"net/http"
	"strconv"
	"time"

	"github.com/c-fido/remindr/api/internal/middleware"
	"github.com/c-fido/remindr/api/internal/models"
	"github.com/c-fido/remindr/api/internal/validation"
	"github.com/go-chi/chi/v5"
	"github.com/google/uuid"
	"github.com/jackc/pgx/v5/pgxpool"
)

type ReminderHandler struct {
	db *pgxpool.Pool
}

func NewReminderHandler(db *pgxpool.Pool) *ReminderHandler {
	return &ReminderHandler{db: db}
}

type createReminderRequest struct {
	ID         *string `json:"id"`
	Message    string  `json:"message"`
	FireAt     int64   `json:"fire_at"`
	Recurrence string  `json:"recurrence"`
}

func (h *ReminderHandler) List(w http.ResponseWriter, r *http.Request) {
	userID, ok := middleware.UserIDFromContext(r.Context())
	if !ok {
		writeError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	includeDeleted := false
	if v := r.URL.Query().Get("include_deleted"); v != "" {
		parsed, err := strconv.ParseBool(v)
		if err != nil {
			writeError(w, http.StatusBadRequest, "include_deleted must be true or false")
			return
		}
		includeDeleted = parsed
	}

	ctx := r.Context()
	query := `SELECT id::text, message, fire_at, recurrence, fired, deleted, updated_at
	          FROM reminders WHERE user_id = $1`
	if !includeDeleted {
		query += ` AND deleted = false`
	}
	query += ` ORDER BY fire_at ASC`

	rows, err := h.db.Query(ctx, query, userID)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}
	defer rows.Close()

	reminders := make([]models.Reminder, 0)
	for rows.Next() {
		var rem models.Reminder
		if err := rows.Scan(&rem.ID, &rem.Message, &rem.FireAt, &rem.Recurrence,
			&rem.Fired, &rem.Deleted, &rem.UpdatedAt); err != nil {
			writeError(w, http.StatusInternalServerError, "database error")
			return
		}
		reminders = append(reminders, rem)
	}
	if err := rows.Err(); err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}

	writeJSON(w, http.StatusOK, models.ReminderListResponse{Reminders: reminders})
}

func (h *ReminderHandler) Create(w http.ResponseWriter, r *http.Request) {
	userID, ok := middleware.UserIDFromContext(r.Context())
	if !ok {
		writeError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	var req createReminderRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, "invalid request body")
		return
	}

	recurrence := req.Recurrence
	if recurrence == "" {
		recurrence = "none"
	}
	if err := validation.ReminderCreate(req.Message, req.FireAt, recurrence); err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}

	id := ""
	if req.ID != nil && *req.ID != "" {
		if _, err := uuid.Parse(*req.ID); err != nil {
			writeError(w, http.StatusBadRequest, "id must be a valid UUID")
			return
		}
		id = *req.ID
	} else {
		id = uuid.New().String()
	}

	now := time.Now().UTC().Unix()
	ctx := r.Context()

	var rem models.Reminder
	err := h.db.QueryRow(ctx,
		`INSERT INTO reminders (id, user_id, message, fire_at, recurrence, fired, deleted, updated_at)
		 VALUES ($1, $2, $3, $4, $5, false, false, $6)
		 RETURNING id::text, message, fire_at, recurrence, fired, deleted, updated_at`,
		id, userID, req.Message, req.FireAt, recurrence, now,
	).Scan(&rem.ID, &rem.Message, &rem.FireAt, &rem.Recurrence, &rem.Fired, &rem.Deleted, &rem.UpdatedAt)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}

	writeJSON(w, http.StatusCreated, rem)
}

func (h *ReminderHandler) Delete(w http.ResponseWriter, r *http.Request) {
	userID, ok := middleware.UserIDFromContext(r.Context())
	if !ok {
		writeError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	id := chi.URLParam(r, "id")
	if _, err := uuid.Parse(id); err != nil {
		writeError(w, http.StatusBadRequest, "id must be a valid UUID")
		return
	}

	now := time.Now().UTC().Unix()
	ctx := r.Context()

	tag, err := h.db.Exec(ctx,
		`UPDATE reminders SET deleted = true, updated_at = $1
		 WHERE id = $2 AND user_id = $3 AND deleted = false`,
		now, id, userID,
	)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}
	if tag.RowsAffected() == 0 {
		// Distinguish missing vs already deleted — both return 404 for simplicity.
		var exists bool
		err := h.db.QueryRow(ctx,
			`SELECT EXISTS(SELECT 1 FROM reminders WHERE id = $1 AND user_id = $2)`,
			id, userID,
		).Scan(&exists)
		if err != nil || !exists {
			writeError(w, http.StatusNotFound, "reminder not found")
			return
		}
		w.WriteHeader(http.StatusNoContent)
		return
	}

	w.WriteHeader(http.StatusNoContent)
}
