#!/usr/bin/env bash
# Start the MPC server, open a Cloudflare quick tunnel, and print the public URL.
set -uo pipefail

: "${MPC_PORT:=8799}"
: "${MPC_UPSTREAM_URL:=http://llama:8080/v1}"
: "${MPC_MODEL:=qwythos}"
export MPC_PORT MPC_UPSTREAM_URL MPC_MODEL

# Generate a bearer token if the operator didn't pin one (the tunnel is public).
if [ -z "${MPC_TOKEN:-}" ]; then
  MPC_TOKEN="$(head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n')"
fi
export MPC_TOKEN

echo "[mcp] MPC server :$MPC_PORT  upstream=$MPC_UPSTREAM_URL  model=$MPC_MODEL"
python3 /app/mcpserver.py &
MPC_PID=$!

# Wait until the local MPC server answers /status.
for _ in $(seq 1 30); do
  if curl -sf -H "Authorization: Bearer ${MPC_TOKEN}" "http://localhost:${MPC_PORT}/status" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done

echo "[mcp] opening cloudflared quick tunnel..."
cloudflared tunnel --no-autoupdate --url "http://localhost:${MPC_PORT}" >/tmp/cf.log 2>&1 &
CF_PID=$!

URL=""
for _ in $(seq 1 40); do
  URL="$(grep -oE 'https://[a-z0-9-]+\.trycloudflare\.com' /tmp/cf.log | head -n1)"
  [ -n "$URL" ] && break
  sleep 1
done

echo ""
echo "=================================================================="
if [ -n "$URL" ]; then
  echo "  OCLI MPC is LIVE at:  $URL"
  echo ""
  echo "  Connect from any OCLI:"
  echo "      /connect $URL --token $MPC_TOKEN"
else
  echo "  Tunnel URL not detected yet. Last cloudflared output:"
  tail -n 15 /tmp/cf.log
fi
echo "=================================================================="
echo ""

# Exit (and let Docker restart) if either process dies.
wait -n "$MPC_PID" "$CF_PID"
echo "[mcp] a process exited — shutting down."
kill "$MPC_PID" "$CF_PID" 2>/dev/null
exit 1
