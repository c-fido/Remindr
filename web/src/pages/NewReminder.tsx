import { useState } from 'react'
import { useFormError } from '../formError'
import { Link, useNavigate } from 'react-router-dom'
import { ApiError, createReminder } from '../api/client'

// Local time (datetime-local has no timezone), one hour out, rounded up to 15 min.
function defaultDatetime() {
  const d = new Date(Date.now() + 3600000)
  d.setMinutes(Math.ceil(d.getMinutes() / 15) * 15, 0, 0)
  return new Date(d.getTime() - d.getTimezoneOffset() * 60000).toISOString().slice(0, 16)
}

export default function NewReminder() {
  const [message, setMessage] = useState('')
  const [datetime, setDatetime] = useState(defaultDatetime)
  const [recurrence, setRecurrence] = useState<'none' | 'daily' | 'weekly'>('none')
  const { alert, fail, clear, fieldProps } = useFormError()
  const [loading, setLoading] = useState(false)
  const navigate = useNavigate()

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault()
    clear()
    if (!message.trim()) { fail('Reminder message is required.', 'message'); return }
    if (!datetime) { fail('A time is required.', 'datetime'); return }
    const fire_at = Math.floor(new Date(datetime).getTime() / 1000)
    if (fire_at <= Date.now() / 1000) { fail('Choose a time in the future.', 'datetime'); return }

    setLoading(true)
    try {
      await createReminder({ message: message.trim(), fire_at, recurrence })
      navigate('/reminders')
    } catch (err) {
      if (err instanceof ApiError) {
        fail(err.message)
      } else {
        fail('Could not reach the API.')
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
          <h1>New reminder</h1>
          {alert}
          <form onSubmit={handleSubmit}>
            <div className="field">
              <label htmlFor="message">Message</label>
              <input
                {...fieldProps('message')}
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
                  {...fieldProps('datetime')}
                  type="datetime-local"
                  value={datetime}
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
