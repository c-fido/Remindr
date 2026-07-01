import { useEffect, useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { ApiError, clearSession, deleteReminder, getEmail, listReminders, type Reminder } from '../api/client'

function formatDate(ts: number) {
  return new Date(ts * 1000).toLocaleDateString('en-US', { month: 'short', day: 'numeric' })
}

function formatTime(ts: number) {
  return new Date(ts * 1000).toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' })
}

export default function Reminders() {
  const [reminders, setReminders] = useState<Reminder[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const navigate = useNavigate()
  const email = getEmail()

  async function load() {
    setError('')
    setLoading(true)
    try {
      setReminders(await listReminders())
    } catch (err) {
      if (err instanceof ApiError) {
        setError(err.message)
      } else {
        setError('Could not reach the API.')
      }
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    load()
  }, [])

  function handleLogout() {
    clearSession()
    navigate('/login')
  }

  async function handleDelete(id: string) {
    try {
      await deleteReminder(id)
      setReminders(rs => rs.filter(r => r.id !== id))
    } catch (err) {
      if (err instanceof ApiError) {
        setError(err.message)
      } else {
        setError('Delete failed.')
      }
    }
  }

  const now = Date.now() / 1000
  const upcoming = reminders.filter(r => !r.fired && (r.fire_at >= now || r.recurrence !== 'none'))
  const fired = reminders.filter(r => r.fired || (r.fire_at < now && r.recurrence === 'none'))
  const sorted = [...upcoming.sort((a, b) => a.fire_at - b.fire_at), ...fired]

  return (
    <div className="app-shell">
      <header className="app-header">
        <div className="app-header-inner">
          <Link to="/reminders" className="app-wordmark">Remindr</Link>
          <div className="header-actions">
            <span className="user-email">{email}</span>
            <Link to="/reminders/new" className="btn btn-ghost">+ New</Link>
            <button className="btn btn-ghost" onClick={handleLogout}>Sign out</button>
          </div>
        </div>
      </header>

      <main className="app-main">
        <div className="page-header">
          <span className="page-title">
            Reminders
            <span className="reminder-count">{upcoming.length}</span>
          </span>
        </div>

        {error && <div className="error-msg">{error}</div>}

        {loading ? (
          <div className="empty-state"><p>Loading…</p></div>
        ) : sorted.length === 0 ? (
          <div className="empty-state">
            <p>No reminders yet.</p>
            <Link to="/reminders/new" className="btn btn-ghost">Add your first reminder</Link>
          </div>
        ) : (
          <div className="ledger">
            {sorted.map(r => (
              <div className="ledger-row" key={r.id}>
                <div className="ledger-time">
                  <span className="ledger-date" style={r.fired ? { opacity: 0.4 } : {}}>{formatDate(r.fire_at)}</span>
                  <span className="ledger-clock">{formatTime(r.fire_at)}</span>
                </div>
                <div className="ledger-rule" />
                <div className="ledger-body">
                  <div className="ledger-content">
                    <div className="ledger-msg" style={r.fired ? { opacity: 0.4 } : {}}>{r.message}</div>
                    <div className="ledger-meta">
                      {r.recurrence !== 'none' && (
                        <span className="badge badge-recurrence">{r.recurrence}</span>
                      )}
                      {r.fired || (r.fire_at < now && r.recurrence === 'none')
                        ? <span className="badge badge-fired">fired</span>
                        : <span className="badge badge-upcoming">upcoming</span>
                      }
                    </div>
                  </div>
                  <button
                    className="btn-danger-ghost"
                    onClick={() => handleDelete(r.id)}
                    aria-label={`Delete reminder: ${r.message}`}
                  >
                    delete
                  </button>
                </div>
              </div>
            ))}
          </div>
        )}
      </main>
    </div>
  )
}
