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

# cloudflared quick-tunnel DNS can take 10-40s to propagate. Don't announce the URL
# as connectable until it actually resolves AND the MPC server answers through it —
# otherwise /connect fails with "Couldn't resolve host name".
READY=""
if [ -n "$URL" ]; then
  echo "[mcp] tunnel URL: $URL"
  echo "[mcp] waiting for the tunnel to become reachable (DNS can take a bit)..."
  for _ in $(seq 1 45); do
    if curl -sf -m 5 -H "Authorization: Bearer ${MPC_TOKEN}" "${URL}/status" >/dev/null 2>&1; then
      READY=1; break
    fi
    sleep 2
  done
fi

echo ""
echo "=================================================================="
if [ -n "$URL" ] && [ -n "$READY" ]; then
  echo "  OCLI MPC is LIVE at:  $URL"
  echo ""
  echo "  Connect from any OCLI:"
  echo "      /connect $URL --token $MPC_TOKEN"
elif [ -n "$URL" ]; then
  echo "  Tunnel is up but not resolvable yet: $URL"
  echo "  Give it ~30s, then in OCLI:  /connect $URL --token $MPC_TOKEN"
  echo "  (if /connect says 'Couldn't resolve host name', just wait and retry)"
else
  echo "  Tunnel URL not detected yet. Last cloudflared output:"
  tail -n 15 /tmp/cf.log
fi
echo "=================================================================="
echo ""

# Optionally publish the live URL + token to a Discord channel via an Incoming
# Webhook. Set DISCORD_WEBHOOK_URL in .env or the environment; it is never
# printed to the logs and never committed to the repo.
if [ -n "$URL" ] && [ -n "$READY" ] && [ -n "${DISCORD_WEBHOOK_URL:-}" ]; then
  echo "[mcp] publishing connection to the Discord webhook..."
  DISCORD_PAYLOAD="$(U="$URL" T="$MPC_TOKEN" python3 -c 'import json,os
u=os.environ["U"]; t=os.environ["T"]
print(json.dumps({
  "username": "GalliviumCloud MPC",
  "content": "**OCLI MPC is LIVE**\nConnect from any OCLI:\n```\n/connect "+u+" --token "+t+"\n```"
}))')"
  if curl -sf -m 10 -H "Content-Type: application/json" \
       -d "$DISCORD_PAYLOAD" "$DISCORD_WEBHOOK_URL" >/dev/null 2>&1; then
    echo "[mcp] posted connection to Discord"
  else
    echo "[mcp] Discord post failed (check DISCORD_WEBHOOK_URL)"
  fi
fi

# Exit (and let Docker restart) if either process dies.
wait -n "$MPC_PID" "$CF_PID"
echo "[mcp] a process exited — shutting down."
kill "$MPC_PID" "$CF_PID" 2>/dev/null
exit 1
