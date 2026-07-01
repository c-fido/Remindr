package handlers

import (
	"encoding/json"
	"errors"
	"net/http"
	"time"

	"github.com/c-fido/remindr/api/internal/middleware"
	"github.com/c-fido/remindr/api/internal/models"
	"github.com/c-fido/remindr/api/internal/sync"
	"github.com/c-fido/remindr/api/internal/validation"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
)

type SyncHandler struct {
	db *pgxpool.Pool
}

func NewSyncHandler(db *pgxpool.Pool) *SyncHandler {
	return &SyncHandler{db: db}
}

func writeDBError(w http.ResponseWriter, err error) {
	var pgErr *pgconn.PgError
	if errors.As(err, &pgErr) {
		if pgErr.Code == "23505" {
			writeError(w, http.StatusConflict,
				"reminder id already exists (log in with the account that owns local data, or clear local reminders)")
			return
		}
	}
	writeError(w, http.StatusInternalServerError, "database error")
}

func (h *SyncHandler) Sync(w http.ResponseWriter, r *http.Request) {
	userID, ok := middleware.UserIDFromContext(r.Context())
	if !ok {
		writeError(w, http.StatusUnauthorized, "unauthorized")
		return
	}

	var req models.SyncRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, "invalid request body: "+err.Error())
		return
	}
	if err := validation.DeviceID(req.DeviceID); err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	if req.LastSyncAt < 0 {
		writeError(w, http.StatusBadRequest, "last_sync_at must be >= 0")
		return
	}

	for _, change := range req.Changes {
		recurrence := change.Recurrence
		if recurrence == "" {
			recurrence = "none"
		}
		if err := validation.SyncChange(change.ID, change.Message, change.FireAt, recurrence, change.Deleted); err != nil {
			writeError(w, http.StatusBadRequest, err.Error())
			return
		}
	}

	ctx := r.Context()
	tx, err := h.db.Begin(ctx)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}
	defer tx.Rollback(ctx)

	serverTime := time.Now().UTC().Unix()
	applied := 0
	conflicts := make([]models.Reminder, 0)

	for _, change := range req.Changes {
		recurrence := change.Recurrence
		if recurrence == "" {
			recurrence = "none"
		}

		var server models.Reminder
		err := tx.QueryRow(ctx,
			`SELECT id::text, message, fire_at, recurrence, fired, deleted, updated_at
			 FROM reminders WHERE id = $1 AND user_id = $2`,
			change.ID, userID,
		).Scan(&server.ID, &server.Message, &server.FireAt, &server.Recurrence,
			&server.Fired, &server.Deleted, &server.UpdatedAt)

		var serverPtr *models.Reminder
		if err == nil {
			serverPtr = &server
		} else if !errors.Is(err, pgx.ErrNoRows) {
			writeDBError(w, err)
			return
		}

		outcome, row := sync.MergeOne(change, serverPtr, serverTime)
		switch outcome {
		case sync.OutcomeInsert:
			_, err = tx.Exec(ctx,
				`INSERT INTO reminders (id, user_id, message, fire_at, recurrence, fired, deleted, updated_at)
				 VALUES ($1, $2, $3, $4, $5, $6, $7, $8)`,
				change.ID, userID, change.Message, change.FireAt, recurrence,
				change.Fired, change.Deleted, change.UpdatedAt,
			)
			if err != nil {
				writeDBError(w, err)
				return
			}
			applied++

		case sync.OutcomeUpdate:
			_, err = tx.Exec(ctx,
				`UPDATE reminders
				 SET message = $1, fire_at = $2, recurrence = $3, fired = $4, deleted = $5, updated_at = $6
				 WHERE id = $7 AND user_id = $8`,
				change.Message, change.FireAt, recurrence, change.Fired, change.Deleted,
				change.UpdatedAt, change.ID, userID,
			)
			if err != nil {
				writeDBError(w, err)
				return
			}
			applied++

		case sync.OutcomeConflict:
			conflicts = append(conflicts, row)

		case sync.OutcomeNoOp:
		}
	}

	rows, err := tx.Query(ctx,
		`SELECT id::text, message, fire_at, recurrence, fired, deleted, updated_at
		 FROM reminders
		 WHERE user_id = $1 AND updated_at > $2
		 ORDER BY updated_at ASC`,
		userID, req.LastSyncAt,
	)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}
	defer rows.Close()

	changes := make([]models.Reminder, 0)
	for rows.Next() {
		var rem models.Reminder
		if err := rows.Scan(&rem.ID, &rem.Message, &rem.FireAt, &rem.Recurrence,
			&rem.Fired, &rem.Deleted, &rem.UpdatedAt); err != nil {
			writeError(w, http.StatusInternalServerError, "database error")
			return
		}
		changes = append(changes, rem)
	}
	if err := rows.Err(); err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}

	if err := tx.Commit(ctx); err != nil {
		writeError(w, http.StatusInternalServerError, "database error")
		return
	}

	writeJSON(w, http.StatusOK, models.SyncResponse{
		ServerTime: serverTime,
		LastSyncAt: serverTime,
		Applied:    applied,
		Conflicts:  conflicts,
		Changes:    changes,
	})
}
