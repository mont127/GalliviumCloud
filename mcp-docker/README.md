# OCLI MPC over Docker + Cloudflare tunnel

Run OCLI's model server anywhere Docker runs, and get a public URL any OCLI can
`/connect` to — no ports to forward, no account needed for the tunnel.

## Quick start (self-contained: serves Qwythos itself)

```bash
cd mcp-docker
docker compose up --build
```

First boot downloads the Qwythos GGUF (~5.5 GB, cached in a volume) and starts
serving it. Watch the `mcp` logs for:

```
==================================================================
  OCLI MPC is LIVE at:  https://<random>.trycloudflare.com

  Connect from any OCLI:
      /connect https://<random>.trycloudflare.com --token <token>
==================================================================
```

Paste that `/connect ...` line into OCLI and you're on Qwythos.

> The bundled `llama` service runs on **CPU** inside Docker, so it's portable
> but not fast. See below to use a GPU or your native model.

## Faster: use a model you already run natively

If you already run Qwythos on the host (native ollama/llama-server uses your
GPU), skip the bundled model and just tunnel to it:

```bash
# host ollama (has `qwythos` after running install_qwythos.sh):
MPC_UPSTREAM_URL=http://host.docker.internal:11434/v1 docker compose up --build mcp

# or a host llama-server on :8080:
MPC_UPSTREAM_URL=http://host.docker.internal:8080/v1  docker compose up --build mcp
```

## NVIDIA GPU host

Edit `docker-compose.yml`: set the `llama` image to
`ghcr.io/ggml-org/llama.cpp:server-cuda` and uncomment the `deploy:` GPU block,
then `docker compose up --build`.

## Config (env)

| var | default | meaning |
|-----|---------|---------|
| `MPC_UPSTREAM_URL` | `http://llama:8080/v1` | OpenAI-compatible upstream to proxy chat to |
| `MPC_MODEL` | `qwythos` | model name sent upstream / reported to OCLI |
| `MPC_TOKEN` | auto-generated | bearer token clients must send (`--token`) |
| `MPC_PORT` | `8799` | MPC port inside the container |

## Notes

- Cloudflare **quick tunnels** are ephemeral — the URL changes each restart.
  For a stable domain, use a named tunnel with a Cloudflare account (swap the
  `cloudflared tunnel --url ...` call in `entrypoint.sh` for `cloudflared tunnel run <name>`).
- The tunnel is public, so a token is always set. Keep it secret.
- The MPC server is a thin proxy (`mcpserver.py`); it needs the upstream to be
  reachable and serving the model.
