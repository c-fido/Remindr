import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import Login from './pages/Login'
import Register from './pages/Register'
import Reminders from './pages/Reminders'
import NewReminder from './pages/NewReminder'
import { isLoggedIn } from './api/client'

function RequireAuth({ children }: { children: React.ReactNode }) {
  return isLoggedIn() ? <>{children}</> : <Navigate to="/login" replace />
}

export default function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/login" element={<Login />} />
        <Route path="/register" element={<Register />} />
        <Route path="/reminders" element={<RequireAuth><Reminders /></RequireAuth>} />
        <Route path="/reminders/new" element={<RequireAuth><NewReminder /></RequireAuth>} />
        <Route path="*" element={<Navigate to={isLoggedIn() ? '/reminders' : '/login'} replace />} />
      </Routes>
    </BrowserRouter>
  )
}
