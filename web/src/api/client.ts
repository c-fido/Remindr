export interface Reminder {
  id: string
  message: string
  fire_at: number
  recurrence: 'none' | 'daily' | 'weekly'
  fired: boolean
  deleted: boolean
  updated_at: number
}

export interface AuthTokens {
  access_token: string
  refresh_token: string
  expires_in: number
}

export class ApiError extends Error {
  status: number

  constructor(status: number, message: string) {
    super(message)
    this.status = status
  }
}

const API_URL = import.meta.env.VITE_API_URL ?? 'http://localhost:8080'

const TOKEN_KEY = 'remindr_access_token'
const REFRESH_KEY = 'remindr_refresh_token'
const EXPIRES_KEY = 'remindr_expires_at'
const EMAIL_KEY = 'remindr_email'

function storeSession(email: string, tokens: AuthTokens) {
  const expiresAt = Math.floor(Date.now() / 1000) + tokens.expires_in
  localStorage.setItem(TOKEN_KEY, tokens.access_token)
  localStorage.setItem(REFRESH_KEY, tokens.refresh_token)
  localStorage.setItem(EXPIRES_KEY, String(expiresAt))
  localStorage.setItem(EMAIL_KEY, email)
}

export function clearSession() {
  localStorage.removeItem(TOKEN_KEY)
  localStorage.removeItem(REFRESH_KEY)
  localStorage.removeItem(EXPIRES_KEY)
  localStorage.removeItem(EMAIL_KEY)
}

export function isLoggedIn() {
  return !!localStorage.getItem(TOKEN_KEY)
}

export function getEmail() {
  return localStorage.getItem(EMAIL_KEY) ?? ''
}

async function parseError(res: Response): Promise<string> {
  try {
    const body = await res.json()
    if (body?.error) return String(body.error)
  } catch {
    /* ignore */
  }
  return `Request failed (${res.status})`
}

async function tryRefresh(): Promise<boolean> {
  const refreshToken = localStorage.getItem(REFRESH_KEY)
  if (!refreshToken) return false

  const res = await fetch(`${API_URL}/v1/auth/refresh`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ refresh_token: refreshToken }),
  })
  if (!res.ok) {
    clearSession()
    return false
  }

  const tokens = (await res.json()) as AuthTokens
  const email = getEmail()
  storeSession(email, tokens)
  return true
}

async function request(path: string, init: RequestInit = {}, auth = true): Promise<Response> {
  const headers = new Headers(init.headers)
  if (!headers.has('Content-Type') && init.body) {
    headers.set('Content-Type', 'application/json')
  }

  if (auth) {
    const token = localStorage.getItem(TOKEN_KEY)
    if (token) headers.set('Authorization', `Bearer ${token}`)
  }

  let res = await fetch(`${API_URL}${path}`, { ...init, headers })

  if (res.status === 401 && auth) {
    const refreshed = await tryRefresh()
    if (refreshed) {
      const token = localStorage.getItem(TOKEN_KEY)
      if (token) headers.set('Authorization', `Bearer ${token}`)
      res = await fetch(`${API_URL}${path}`, { ...init, headers })
    }
  }

  return res
}

export async function login(email: string, password: string): Promise<void> {
  const res = await request('/v1/auth/login', {
    method: 'POST',
    body: JSON.stringify({ email, password }),
  }, false)

  if (!res.ok) throw new ApiError(res.status, await parseError(res))

  const tokens = (await res.json()) as AuthTokens
  storeSession(email, tokens)
}

export async function register(email: string, password: string): Promise<void> {
  const res = await request('/v1/auth/register', {
    method: 'POST',
    body: JSON.stringify({ email, password }),
  }, false)

  if (!res.ok) throw new ApiError(res.status, await parseError(res))

  const tokens = (await res.json()) as AuthTokens
  storeSession(email, tokens)
}

export async function listReminders(): Promise<Reminder[]> {
  const res = await request('/v1/reminders')
  if (!res.ok) throw new ApiError(res.status, await parseError(res))

  const body = await res.json()
  return body.reminders as Reminder[]
}

export async function createReminder(input: {
  message: string
  fire_at: number
  recurrence: 'none' | 'daily' | 'weekly'
}): Promise<Reminder> {
  const res = await request('/v1/reminders', {
    method: 'POST',
    body: JSON.stringify(input),
  })
  if (!res.ok) throw new ApiError(res.status, await parseError(res))
  return (await res.json()) as Reminder
}

export async function deleteReminder(id: string): Promise<void> {
  const res = await request(`/v1/reminders/${id}`, { method: 'DELETE' })
  if (!res.ok) throw new ApiError(res.status, await parseError(res))
}

export { API_URL }
