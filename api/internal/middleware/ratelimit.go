package middleware

import (
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

const (
	rateLimitWindow = time.Minute
	rateLimitMax    = 10
)

type rateLimiter struct {
	mu   sync.Mutex
	hits map[string][]time.Time
}

// NewRateLimiter creates an in-memory per-IP rate limiter for auth routes.
func NewRateLimiter() *rateLimiter {
	return &rateLimiter{hits: make(map[string][]time.Time)}
}

func (l *rateLimiter) allow(key string) bool {
	now := time.Now()
	cutoff := now.Add(-rateLimitWindow)

	l.mu.Lock()
	defer l.mu.Unlock()

	times := l.hits[key]
	filtered := times[:0]
	for _, t := range times {
		if t.After(cutoff) {
			filtered = append(filtered, t)
		}
	}
	if len(filtered) >= rateLimitMax {
		l.hits[key] = filtered
		return false
	}

	l.hits[key] = append(filtered, now)
	return true
}

func clientIP(r *http.Request) string {
	if fwd := r.Header.Get("X-Forwarded-For"); fwd != "" {
		parts := strings.Split(fwd, ",")
		return strings.TrimSpace(parts[0])
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

// RateLimitAuth limits auth endpoints to 10 requests per minute per IP.
func RateLimitAuth(limiter *rateLimiter) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			if !limiter.allow(clientIP(r)) {
				http.Error(w, `{"error":"too many requests"}`, http.StatusTooManyRequests)
				return
			}
			next.ServeHTTP(w, r)
		})
	}
}
