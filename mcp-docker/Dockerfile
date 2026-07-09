# OCLI MPC server + Cloudflare quick-tunnel.
# Wraps mcpserver.py (the MPC protocol OCLI's /connect expects) and prints a
# public https URL on start. Proxies chat to an OpenAI-compatible upstream
# (the bundled llama.cpp service by default; override MPC_UPSTREAM_URL to use
# your own ollama / llama-server).
FROM python:3.12-slim

ARG TARGETARCH
RUN apt-get update \
    && apt-get install -y --no-install-recommends curl ca-certificates bash \
    && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir "openai>=1.0"

# cloudflared (multi-arch): gives a zero-config https://<name>.trycloudflare.com URL
RUN arch="$( [ "$TARGETARCH" = "arm64" ] && echo arm64 || echo amd64 )" \
    && curl -fsSL -o /usr/local/bin/cloudflared \
       "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-${arch}" \
    && chmod +x /usr/local/bin/cloudflared

WORKDIR /app
# build context is the repo root (see docker-compose.yml: context: ..)
COPY mcpserver.py /app/mcpserver.py
COPY mcp-docker/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

ENV MPC_HOST=0.0.0.0 \
    MPC_PORT=8799 \
    MPC_MODEL=qwythos \
    MPC_BACKEND=llama-cpp \
    MPC_UPSTREAM_URL=http://llama:8080/v1 \
    MPC_UPSTREAM_KEY=none

EXPOSE 8799
ENTRYPOINT ["/app/entrypoint.sh"]
