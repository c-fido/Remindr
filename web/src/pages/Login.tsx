import { useState } from 'react'
import { useFormError } from '../formError'
import { Link, useNavigate } from 'react-router-dom'
import { ApiError, login } from '../api/client'

export default function Login() {
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const { alert, fail, clear, fieldProps } = useFormError()
  const [loading, setLoading] = useState(false)
  const navigate = useNavigate()

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault()
    clear()
    if (!email) { fail('Enter your email.', 'email'); return }
    if (!password) { fail('Enter your password.', 'password'); return }

    setLoading(true)
    try {
      await login(email, password)
      navigate('/reminders')
    } catch (err) {
      if (err instanceof ApiError && err.status === 401) {
        fail('Invalid email or password.')
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
        <h1>Sign in</h1>
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
              autoComplete="current-password"
              value={password}
              onChange={e => setPassword(e.target.value)}
              placeholder="••••••••"
              disabled={loading}
            />
          </div>
          <button type="submit" className="btn btn-primary" style={{ marginTop: '0.5rem' }} disabled={loading}>
            {loading ? 'Signing in…' : 'Sign in'}
          </button>
        </form>
      </div>
      <p className="auth-link">
        No account? <Link to="/register">Register</Link>
      </p>
    </main>
  )
}
