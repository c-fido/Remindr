package sync

import (
	"github.com/c-fido/remindr/api/internal/models"
)

const ClockSkewSec int64 = 300

type Outcome int

const (
	OutcomeInsert Outcome = iota
	OutcomeUpdate
	OutcomeNoOp
	OutcomeConflict
)

// MergeOne decides how to reconcile a client change against an existing server row.
// server is nil when the reminder id is unknown on the server.
func MergeOne(client models.Reminder, server *models.Reminder, serverTime int64) (Outcome, models.Reminder) {
	if server == nil {
		if client.UpdatedAt > serverTime+ClockSkewSec {
			return OutcomeConflict, client
		}
		return OutcomeInsert, client
	}

	if client.UpdatedAt == server.UpdatedAt {
		return OutcomeNoOp, *server
	}

	if client.UpdatedAt > server.UpdatedAt {
		if client.UpdatedAt > serverTime+ClockSkewSec {
			return OutcomeConflict, *server
		}
		return OutcomeUpdate, client
	}

	return OutcomeConflict, *server
}
