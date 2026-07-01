package config

import (
	"fmt"
	"os"
	"strings"
)

type Config struct {
	Port        string
	DatabaseURL string
	JWTSecret   string
	WebOrigin   string
}

func Load() (Config, error) {
	cfg := Config{
		Port:        envOr("PORT", "8080"),
		DatabaseURL: cleanEnv("DATABASE_URL"),
		JWTSecret:   cleanEnv("JWT_SECRET"),
		WebOrigin:   envOr("WEB_ORIGIN", "http://localhost:5173"),
	}

	if cfg.DatabaseURL == "" {
		return cfg, fmt.Errorf("DATABASE_URL is required")
	}
	if cfg.JWTSecret == "" {
		return cfg, fmt.Errorf("JWT_SECRET is required")
	}

	return cfg, nil
}

func envOr(key, fallback string) string {
	if v := cleanEnv(key); v != "" {
		return v
	}
	return fallback
}

// cleanEnv trims whitespace and strips accidental newlines from pasted secrets.
func cleanEnv(key string) string {
	v := os.Getenv(key)
	v = strings.TrimSpace(v)
	v = strings.ReplaceAll(v, "\n", "")
	v = strings.ReplaceAll(v, "\r", "")
	return v
}
