import { useEffect, useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { API_URL, ApiError, clearSession, deleteReminder, getEmail, listReminders, type Reminder } from '../api/client'

const PERIOD = { daily: 86400, weekly: 604800 } as const

// Mirrors reminderd: a recurring reminder advances by whole periods until it's in the future.
function nextFireAt(r: Reminder, now: number) {
  if (r.recurrence === 'none' || r.fire_at >= now) return r.fire_at
  const p = PERIOD[r.recurrence]
  return r.fire_at + Math.ceil((now - r.fire_at) / p) * p
}

const rtf = new Intl.RelativeTimeFormat(undefined, { numeric: 'auto' })
const STEPS: [Intl.RelativeTimeFormatUnit, number][] = [
  ['minute', 60], ['hour', 3600], ['day', 86400], ['month', 3888000], ['year', 31536000],
]

// "in 2 hours", "tomorrow", "3 days ago" — the same grammar `remind` speaks.
function formatRelative(ts: number, now: number) {
  const diff = ts - now
  if (Math.abs(diff) < 60) return rtf.format(0, 'minute')
  let i = 0
  while (i < STEPS.length - 1 && Math.abs(diff) >= STEPS[i + 1][1]) i++
  const [unit, secs] = STEPS[i]
  return rtf.format(Math.round(diff / secs), unit)
}

function formatAbsolute(ts: number) {
  const d = new Date(ts * 1000)
  const date = d.toLocaleDateString(undefined, {
    weekday: 'short',
    month: 'short',
    day: 'numeric',
    year: d.getFullYear() === new Date().getFullYear() ? undefined : 'numeric',
  })
  const time = d.toLocaleTimeString(undefined, { hour: 'numeric', minute: '2-digit' })
  return [date, time]
}

export default function Reminders() {
  const [reminders, setReminders] = useState<Reminder[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [confirmId, setConfirmId] = useState<string | null>(null)
  const [deletingId, setDeletingId] = useState<string | null>(null)
  const [updatedAt, setUpdatedAt] = useState<number | null>(null)
  const [, setTick] = useState(0)
  const [copied, setCopied] = useState(false)
  const navigate = useNavigate()
  const email = getEmail()

  // silent: background refresh — keep the list on screen, no loading state.
  async function load(silent = false) {
    if (!silent) {
      setError('')
      setLoading(true)
    }
    try {
      setReminders(await listReminders())
      setUpdatedAt(Date.now() / 1000)
      if (silent) setError('')
    } catch (err) {
      handleError(err, "Couldn't reach the sync server. Reminders on your machine are unaffected.")
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    load()
    // The daemon pushes CLI changes every 60s; poll at the same pace and on tab focus.
    const refresh = () => { if (document.visibilityState === 'visible') load(true) }
    const poll = setInterval(refresh, 60000)
    const tick = setInterval(() => setTick(t => t + 1), 15000)
    document.addEventListener('visibilitychange', refresh)
    return () => {
      clearInterval(poll)
      clearInterval(tick)
      document.removeEventListener('visibilitychange', refresh)
    }
  }, [])

  function handleError(err: unknown, fallback: string) {
    if (err instanceof ApiError && err.status === 401) {
      clearSession()
      navigate('/login')
    } else {
      setError(err instanceof ApiError ? err.message : fallback)
    }
  }

  function handleLogout() {
    clearSession()
    navigate('/login')
  }

  async function handleDelete(id: string) {
    setError('')
    setDeletingId(id)
    try {
      await deleteReminder(id)
      setReminders(rs => rs.filter(r => r.id !== id))
    } catch (err) {
      handleError(err, "Couldn't delete that reminder. Check your connection and try again.")
    } finally {
      setDeletingId(null)
      setConfirmId(null)
    }
  }

  const now = Date.now() / 1000
  const isFired = (r: Reminder) => r.fired || (r.recurrence === 'none' && r.fire_at < now)
  const upcoming = reminders
    .filter(r => !isFired(r))
    .map(r => ({ ...r, fire_at: nextFireAt(r, now) }))
    .sort((a, b) => a.fire_at - b.fire_at)
  const fired = reminders.filter(isFired).sort((a, b) => b.fire_at - a.fire_at)
  const total = reminders.length
  const cliSetup = [
    `export REMINDR_API_URL=${API_URL}`,
    `remind login ${email || 'you@example.com'}`,
    'remind call mom in 2 hours',
  ]

  async function copySetup() {
    try {
      await navigator.clipboard.writeText(cliSetup.join('\n'))
      setCopied(true)
      setTimeout(() => setCopied(false), 2000)
    } catch {
      /* clipboard blocked; the text is still selectable */
    }
  }

  function renderRow(r: Reminder, past: boolean) {
    return (
      <li className={past ? 'ledger-row is-fired' : 'ledger-row'} key={r.id}>
        <time className="ledger-time" dateTime={new Date(r.fire_at * 1000).toISOString()}>
          <span className="ledger-date">{formatRelative(r.fire_at, now)}</span>
          <span className="ledger-clock">
            {formatAbsolute(r.fire_at).map(part => <span key={part}>{part}</span>)}
          </span>
        </time>
        <div className="ledger-rule" />
        <div className="ledger-body">
          <div className="ledger-content">
            <div className="ledger-msg" title={r.message}>{r.message}</div>
            {r.recurrence !== 'none' && (
              <div className="ledger-meta">
                <span className="badge badge-recurrence">repeats {r.recurrence}</span>
              </div>
            )}
          </div>
          {confirmId === r.id ? (
            <div className="ledger-confirm">
              <button
                className="btn-danger-ghost is-danger"
                onClick={() => handleDelete(r.id)}
                disabled={deletingId === r.id}
                aria-label={`Confirm delete: ${r.message}`}
                autoFocus
              >
                {deletingId === r.id ? 'deleting…' : 'delete'}
              </button>
              <button
                className="btn-danger-ghost"
                onClick={() => setConfirmId(null)}
                disabled={deletingId === r.id}
              >
                keep
              </button>
            </div>
          ) : (
            <button
              className="btn-danger-ghost"
              onClick={() => setConfirmId(r.id)}
              aria-label={`Delete reminder: ${r.message}`}
            >
              delete
            </button>
          )}
        </div>
      </li>
    )
  }

  return (
    <div className="app-shell">
      <header className="app-header">
        <div className="app-header-inner">
          <Link to="/reminders" className="app-wordmark">Remindr</Link>
          <div className="header-actions">
            <span className="user-email">{email}</span>
            <Link to="/reminders/new" className="btn btn-primary">+ New</Link>
            <button className="btn btn-ghost" onClick={handleLogout}>Sign out</button>
          </div>
        </div>
      </header>

      <main className="app-main">
        <h1 className="sr-only">Reminders</h1>
        {error && total > 0 && <div className="error-msg" role="alert">{error}</div>}

        {loading ? (
          <div className="empty-state"><p>Loading reminders…</p></div>
        ) : error && total === 0 ? (
          <div className="empty-state" role="alert">
            <p>{error}</p>
            <button className="btn btn-ghost" onClick={() => load()}>Retry</button>
          </div>
        ) : total === 0 ? (
          <div className="onboard">
            <h2 className="onboard-title">No reminders yet.</h2>
            <p className="onboard-lede">
              Remindr lives in your terminal. Link the CLI and anything you schedule there shows up here.
            </p>
            <figure className="terminal">
              <figcaption className="terminal-bar">
                <span>terminal</span>
                <button className="terminal-copy" onClick={copySetup}>
                  {copied ? 'copied' : 'copy'}
                </button>
              </figcaption>
              <pre>
                {cliSetup.map(line => (
                  <code key={line}><span className="terminal-prompt" aria-hidden="true">$ </span>{line}</code>
                ))}
              </pre>
            </figure>
            <p className="onboard-alt">
              No CLI handy? <Link to="/reminders/new">Add one from the web</Link>.
            </p>
          </div>
        ) : (
          <>
            <section className="ledger-section">
              <div className="ledger-head">
                <h2 className="page-title">
                  Upcoming <span className="reminder-count">{upcoming.length}</span>
                </h2>
                {updatedAt && (
                  <span className="sync-status" title="Refreshes every minute and when you return to this tab">
                    {Date.now() / 1000 - updatedAt < 60
                      ? 'updated just now'
                      : `updated ${formatRelative(updatedAt, Date.now() / 1000)}`}
                  </span>
                )}
              </div>
              {upcoming.length > 0
                ? <ul className="ledger">{upcoming.map(r => renderRow(r, false))}</ul>
                : <p className="ledger-none">Nothing scheduled.</p>}
            </section>
            {fired.length > 0 && (
              <section className="ledger-section">
                <h2 className="page-title">
                  Fired <span className="reminder-count">{fired.length}</span>
                </h2>
                <ul className="ledger">{fired.map(r => renderRow(r, true))}</ul>
              </section>
            )}
          </>
        )}
      </main>
    </div>
  )
}
