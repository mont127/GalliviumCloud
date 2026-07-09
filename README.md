# GalliviumCloud

Run an **[OCLI](https://github.com/mont127/LOREA)-compatible model server anywhere Docker runs**,
and get a **public URL** any OCLI client can `/connect` to — no port forwarding, no
account needed for the tunnel.

It's two small pieces:

- **`mcpserver.py`** — a dependency-light server that speaks OCLI's **MPC protocol**
  (what `/connect` talks to) and proxies chat to any **OpenAI-compatible upstream**
  (ollama, a llama.cpp `llama-server`, or a cloud provider), streaming results back as
  NDJSON.
- **A Docker + Compose stack** that serves the default model (**Qwythos-9B, 1M context**)
  and opens a **Cloudflare quick tunnel**, printing the public URL on start.

---

## Quick start (self-contained)

```bash
docker compose up --build
```

First boot downloads the Qwythos GGUF (~5.5 GB, cached in a volume) and serves it, then
prints:

```
==================================================================
  OCLI MPC is LIVE at:  https://<name>.trycloudflare.com

  Connect from any OCLI:
      /connect https://<name>.trycloudflare.com --token <token>
==================================================================
```

Paste that `/connect …` line into OCLI and you're running on Qwythos from anywhere.

> The bundled `llama` service runs on **CPU** inside Docker — portable, but not fast for
> a 9B. See below for GPU / native options.

## Use a model you already run (faster)

Skip the bundled model and tunnel to a native one (uses your GPU):

```bash
# host ollama (has `qwythos` after OCLI's install_qwythos.sh):
MPC_UPSTREAM_URL=http://host.docker.internal:11434/v1 docker compose up --build mcp

# or a host llama-server on :8080:
MPC_UPSTREAM_URL=http://host.docker.internal:8080/v1  docker compose up --build mcp
```

## NVIDIA GPU host

In `docker-compose.yml`, set the `llama` image to `ghcr.io/ggml-org/llama.cpp:server-cuda`
and uncomment the `deploy:` GPU block, then `docker compose up --build`.

## Run without Docker

```bash
pip install openai
python3 mcpserver.py            # 0.0.0.0:8799, upstream = ollama, model = qwythos
```

Then, separately, expose it however you like (`cloudflared tunnel --url http://localhost:8799`,
an SSH tunnel, a reverse proxy, …) and `/connect` to the resulting URL.

---

## Configuration (env)

| var | default | meaning |
|-----|---------|---------|
| `MPC_UPSTREAM_URL` | `http://llama:8080/v1` | OpenAI-compatible upstream to proxy chat to |
| `MPC_UPSTREAM_KEY` | `none` | upstream API key (llama-server ignores it) |
| `MPC_MODEL` | `qwythos` | model name sent upstream / reported to OCLI |
| `MPC_TOKEN` | auto-generated | bearer token clients must send (`--token`) |
| `MPC_PORT` | `8799` | port the MPC server listens on |
| `MPC_MAX_TOKENS` | `4096` | per-reply cap |
| `LLAMA_CTX` | `32768` | bundled `llama` service context size — must exceed OCLI's ~10k-token system prompt; raise for longer chats (needs more RAM) |

The tunnel is public, so a token is always set (generated if you don't pin one) — keep it secret.

---

## MPC protocol (what `mcpserver.py` implements)

OCLI's `/connect` client speaks this; the server answers:

| Method + path | Purpose |
|---|---|
| `GET /status` | `{ok, version, selected_backend, selected_model}` |
| `GET /models` | `{backend, models[], installed[]}` |
| `POST /select` | `{backend, model}` → `{selected_backend, selected_model}` |
| `POST /chat` | chat completion; `{"stream": true}` streams **NDJSON** events (`{"type":"content"}` … `{"type":"metadata"}` … `{"type":"done"}`, or `{"type":"error","retryable":…}`) |
| `GET /downloads`, `POST /download` / `/cancel` / `/delete` | model-management stubs (manage models out-of-band) |

Auth: optional `Authorization: Bearer <MPC_TOKEN>` on every request.

GLM/Qwen-style reasoning (`reasoning_content`) is wrapped in `<thought>…</thought>` so OCLI
renders it as "thinking".

## License

See [LICENSE](LICENSE).
