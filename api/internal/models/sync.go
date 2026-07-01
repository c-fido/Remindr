package models

type SyncRequest struct {
	DeviceID   string     `json:"device_id"`
	LastSyncAt int64      `json:"last_sync_at"`
	Changes    []Reminder `json:"changes"`
}

type SyncResponse struct {
	ServerTime int64      `json:"server_time"`
	LastSyncAt int64      `json:"last_sync_at"`
	Applied    int        `json:"applied"`
	Conflicts  []Reminder `json:"conflicts"`
	Changes    []Reminder `json:"changes"`
}
