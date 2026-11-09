import { useRef, useState } from 'react'

const ERROR_ID = 'form-error'

// One form-level error, optionally tied to a field. Each fail() gets a new key so the
// role="alert" node remounts and screen readers re-announce even an identical message.
export function useFormError() {
  const [err, setErr] = useState<{ msg: string; field?: string; n: number } | null>(null)
  const count = useRef(0) // survives clear(), so a repeated message still gets a fresh key

  function fail(msg: string, field?: string) {
    setErr({ msg, field, n: ++count.current })
    // Next frame: after an async submit the field is still disabled until loading resets.
    if (field) requestAnimationFrame(() => document.getElementById(field)?.focus())
  }

  function fieldProps(id: string) {
    const invalid = err?.field === id
    return {
      id,
      'aria-invalid': invalid || undefined,
      'aria-describedby': invalid ? ERROR_ID : undefined,
    }
  }

  const alert = err && (
    <div className="error-msg" role="alert" id={ERROR_ID} key={err.n}>{err.msg}</div>
  )

  return { alert, fail, clear: () => setErr(null), fieldProps }
}
