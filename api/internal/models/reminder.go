package models

type Reminder struct {
	ID         string `json:"id"`
	UserID     string `json:"-"`
	Message    string `json:"message"`
	FireAt     int64  `json:"fire_at"`
	Recurrence string `json:"recurrence"`
	Fired      bool   `json:"fired"`
	Deleted    bool   `json:"deleted"`
	UpdatedAt  int64  `json:"updated_at"`
}

type ReminderListResponse struct {
	Reminders []Reminder `json:"reminders"`
}
