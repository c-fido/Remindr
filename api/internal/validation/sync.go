package validation

import (
	"fmt"
	"strings"

	"github.com/google/uuid"
)

func DeviceID(deviceID string) error {
	if deviceID == "" {
		return fmt.Errorf("device_id is required")
	}
	if _, err := uuid.Parse(deviceID); err != nil {
		return fmt.Errorf("device_id must be a valid UUID")
	}
	return nil
}

func SyncChange(id string, message string, _ int64, recurrence string, deleted bool) error {
	if _, err := uuid.Parse(id); err != nil {
		return fmt.Errorf("id must be a valid UUID")
	}
	if deleted {
		return nil
	}
	if recurrence == "" {
		recurrence = "none"
	}
	return syncReminderFields(message, recurrence)
}

// syncReminderFields validates a pushed change without the ±1 year fire_at window, the daemon may sync already-fired or older local reminders.
func syncReminderFields(message string, recurrence string) error {
	message = strings.TrimSpace(message)
	if len(message) < 1 || len(message) > MaxMessageLen {
		return fmt.Errorf("message must be 1-%d characters", MaxMessageLen)
	}
	if _, ok := validRecurrence[recurrence]; !ok {
		return fmt.Errorf("recurrence must be none, daily, or weekly")
	}
	return nil
}
