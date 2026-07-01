import { useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { ApiError, createReminder } from '../api/client'

export default function NewReminder() {
  const [message, setMessage] = useState('')
  const [datetime, setDatetime] = useState('')
  const [recurrence, setRecurrence] = useState<'none' | 'daily' | 'weekly'>('none')
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)
  const navigate = useNavigate()

  function defaultDatetime() {
    const d = new Date(Date.now() + 3600000)
    d.setMinutes(Math.ceil(d.getMinutes() / 15) * 15, 0, 0)
    return d.toISOString().slice(0, 16)
  }

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault()
    setError('')
    if (!message.trim()) { setError('Reminder message is required.'); return }
    if (!datetime) { setError('A time is required.'); return }
    const fire_at = Math.floor(new Date(datetime).getTime() / 1000)
    if (fire_at <= Date.now() / 1000) { setError('Choose a time in the future.'); return }

    setLoading(true)
    try {
      await createReminder({ message: message.trim(), fire_at, recurrence })
      navigate('/reminders')
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

  return (
    <div className="app-shell">
      <header className="app-header">
        <div className="app-header-inner">
          <Link to="/reminders" className="app-wordmark">Remindr</Link>
          <div className="header-actions" />
        </div>
      </header>

      <main className="app-main">
        <Link to="/reminders" className="back-link">← Reminders</Link>

        <div className="form-card">
          <h2>New reminder</h2>
          {error && <div className="error-msg">{error}</div>}
          <form onSubmit={handleSubmit}>
            <div className="field">
              <label htmlFor="message">Message</label>
              <input
                id="message"
                type="text"
                maxLength={500}
                autoFocus
                value={message}
                onChange={e => setMessage(e.target.value)}
                placeholder="e.g. call dentist"
                disabled={loading}
              />
            </div>
            <div className="form-row">
              <div className="field">
                <label htmlFor="datetime">When</label>
                <input
                  id="datetime"
                  type="datetime-local"
                  value={datetime || defaultDatetime()}
                  onChange={e => setDatetime(e.target.value)}
                  disabled={loading}
                />
              </div>
              <div className="field">
                <label htmlFor="recurrence">Repeat</label>
                <select
                  id="recurrence"
                  value={recurrence}
                  onChange={e => setRecurrence(e.target.value as typeof recurrence)}
                  disabled={loading}
                >
                  <option value="none">Does not repeat</option>
                  <option value="daily">Daily</option>
                  <option value="weekly">Weekly</option>
                </select>
              </div>
            </div>
            <div className="form-actions">
              <button type="submit" className="btn btn-primary" disabled={loading}>
                {loading ? 'Saving…' : 'Save reminder'}
              </button>
              <Link to="/reminders" className="btn btn-ghost">Cancel</Link>
            </div>
          </form>
        </div>
      </main>
    </div>
  )
}
