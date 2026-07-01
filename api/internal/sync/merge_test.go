package sync_test

import (
	"testing"

	"github.com/c-fido/remindr/api/internal/models"
	"github.com/c-fido/remindr/api/internal/sync"
)

func reminder(id string, updatedAt int64, deleted bool) models.Reminder {
	return models.Reminder{
		ID:         id,
		Message:    "msg",
		FireAt:     1_700_000_000,
		Recurrence: "none",
		UpdatedAt:  updatedAt,
		Deleted:    deleted,
	}
}

func TestMergeOne_clientWinsWhenNewer(t *testing.T) {
	server := reminder("a", 100, false)
	client := reminder("a", 200, false)
	client.Message = "client wins"

	outcome, row := sync.MergeOne(client, &server, 150)
	if outcome != sync.OutcomeUpdate {
		t.Fatalf("outcome = %v, want OutcomeUpdate", outcome)
	}
	if row.Message != "client wins" {
		t.Fatalf("row.Message = %q, want client wins", row.Message)
	}
}

func TestMergeOne_serverWinsWhenNewer(t *testing.T) {
	server := reminder("a", 300, false)
	server.Message = "server wins"
	client := reminder("a", 200, false)

	outcome, row := sync.MergeOne(client, &server, 350)
	if outcome != sync.OutcomeConflict {
		t.Fatalf("outcome = %v, want OutcomeConflict", outcome)
	}
	if row.Message != "server wins" {
		t.Fatalf("row.Message = %q, want server wins", row.Message)
	}
}

func TestMergeOne_insertUnknownID(t *testing.T) {
	client := reminder("new-id", 100, false)

	outcome, row := sync.MergeOne(client, nil, 150)
	if outcome != sync.OutcomeInsert {
		t.Fatalf("outcome = %v, want OutcomeInsert", outcome)
	}
	if row.ID != "new-id" {
		t.Fatalf("row.ID = %q, want new-id", row.ID)
	}
}

func TestMergeOne_tombstonePropagates(t *testing.T) {
	server := reminder("a", 100, false)
	client := reminder("a", 200, true)

	outcome, row := sync.MergeOne(client, &server, 150)
	if outcome != sync.OutcomeUpdate {
		t.Fatalf("outcome = %v, want OutcomeUpdate", outcome)
	}
	if !row.Deleted {
		t.Fatal("expected deleted tombstone from client to win")
	}
}

func TestMergeOne_equalTimestampsNoOp(t *testing.T) {
	server := reminder("a", 100, false)
	client := reminder("a", 100, false)
	client.Message = "different"

	outcome, _ := sync.MergeOne(client, &server, 150)
	if outcome != sync.OutcomeNoOp {
		t.Fatalf("outcome = %v, want OutcomeNoOp", outcome)
	}
}

func TestMergeOne_clockSkewConflict(t *testing.T) {
	server := reminder("a", 100, false)
	client := reminder("a", 500, false)

	outcome, row := sync.MergeOne(client, &server, 150)
	if outcome != sync.OutcomeConflict {
		t.Fatalf("outcome = %v, want OutcomeConflict", outcome)
	}
	if row.ID != server.ID {
		t.Fatalf("expected server row in conflict, got %q", row.ID)
	}
}
