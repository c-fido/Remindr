#!/usr/bin/env bash
# Blocks commits that likely contain secrets.
# Install: cp scripts/pre-commit-secret-check.sh .git/hooks/pre-commit && chmod +x .git/hooks/pre-commit

set -euo pipefail

blocked=0

# Reject staged .env files (use .env.example for templates only)
while IFS= read -r -d '' file; do
  echo "pre-commit: refuse to commit $file (use .env locally, .env.example for placeholders)"
  blocked=1
done < <(git diff --cached --name-only -z | grep -zE '(^|/)\.env$|/\.env\.local$' || true)

# Reject Neon-style passwords / connection strings in staged content
if git diff --cached | grep -qE 'npg_[A-Za-z0-9]+|postgresql://[^[:space:]]+:[^@/[:space:]]+@'; then
  echo "pre-commit: staged diff looks like a database URL or Neon password"
  blocked=1
fi

if [ "$blocked" -ne 0 ]; then
  echo "pre-commit: fix or unstage before committing."
  exit 1
fi
