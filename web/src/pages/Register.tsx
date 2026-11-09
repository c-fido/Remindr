import { useState } from 'react'
import { useFormError } from '../formError'
import { Link, useNavigate } from 'react-router-dom'
import { ApiError, register } from '../api/client'

export default function Register() {
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [confirm, setConfirm] = useState('')
  const { alert, fail, clear, fieldProps } = useFormError()
  const [loading, setLoading] = useState(false)
  const navigate = useNavigate()

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault()
    clear()
    if (!email) { fail('Enter your email.', 'email'); return }
    if (!password) { fail('Choose a password.', 'password'); return }
    if (password.length < 8) { fail('Password must be at least 8 characters.', 'password'); return }
    if (password !== confirm) { fail('Passwords do not match.', 'confirm'); return }

    setLoading(true)
    try {
      await register(email, password)
      navigate('/reminders')
    } catch (err) {
      if (err instanceof ApiError && err.status === 409) {
        fail('Email already registered.', 'email')
      } else if (err instanceof ApiError) {
        fail(err.message)
      } else {
        fail("Couldn't reach the sync server. Check your connection, then try again.")
      }
    } finally {
      setLoading(false)
    }
  }

  return (
    <main className="auth-shell">
      <div className="auth-wordmark">Remindr</div>
      <p className="auth-sub">your reminders, everywhere</p>
      <div className="auth-card">
        <h1>Create account</h1>
        {alert}
        <form onSubmit={handleSubmit}>
          <div className="field">
            <label htmlFor="email">Email</label>
            <input
              {...fieldProps('email')}
              type="email"
              autoComplete="email"
              value={email}
              onChange={e => setEmail(e.target.value)}
              placeholder="you@example.com"
              disabled={loading}
            />
          </div>
          <div className="field">
            <label htmlFor="password">Password</label>
            <input
              {...fieldProps('password')}
              type="password"
              autoComplete="new-password"
              value={password}
              onChange={e => setPassword(e.target.value)}
              placeholder="min. 8 characters"
              disabled={loading}
            />
          </div>
          <div className="field">
            <label htmlFor="confirm">Confirm password</label>
            <input
              {...fieldProps('confirm')}
              type="password"
              autoComplete="new-password"
              value={confirm}
              onChange={e => setConfirm(e.target.value)}
              placeholder="••••••••"
              disabled={loading}
            />
          </div>
          <button type="submit" className="btn btn-primary" style={{ marginTop: '0.5rem' }} disabled={loading}>
            {loading ? 'Creating account…' : 'Create account'}
          </button>
        </form>
      </div>
      <p className="auth-link">
        Already have an account? <Link to="/login">Sign in</Link>
      </p>
    </main>
  )
}
