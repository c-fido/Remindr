package validation

import (
	"fmt"
	"strings"
	"time"
)

const (
	MinPasswordLen = 8
	MaxMessageLen  = 500
)

var validRecurrence = map[string]struct{}{
	"none":   {},
	"daily":  {},
	"weekly": {},
}

func AuthCredentials(email, password string) error {
	email = strings.TrimSpace(email)
	if email == "" || !strings.Contains(email, "@") || len(email) > 254 {
		return fmt.Errorf("invalid email")
	}
	if len(password) < MinPasswordLen {
		return fmt.Errorf("password must be at least %d characters", MinPasswordLen)
	}
	return nil
}

func FireAt(fireAt int64) error {
	now := time.Now().UTC()
	min := now.AddDate(-1, 0, 0).Unix()
	max := now.AddDate(1, 0, 0).Unix()
	if fireAt < min || fireAt > max {
		return fmt.Errorf("fire_at must be within one year of now")
	}
	return nil
}

func ReminderCreate(message string, fireAt int64, recurrence string) error {
	message = strings.TrimSpace(message)
	if len(message) < 1 || len(message) > MaxMessageLen {
		return fmt.Errorf("message must be 1-%d characters", MaxMessageLen)
	}
	if _, ok := validRecurrence[recurrence]; !ok {
		return fmt.Errorf("recurrence must be none, daily, or weekly")
	}
	return FireAt(fireAt)
}
