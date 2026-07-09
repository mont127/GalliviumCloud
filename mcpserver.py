#!/usr/bin/env python3
"""OCLI MPC server.

Speaks the MPC protocol that OCLI's `/connect` client expects, and proxies chat
to a local OpenAI-compatible upstream (ollama by default, or a llama.cpp
llama-server). Default model: qwythos.

Run it:
    python3 mcpserver.py                 # 0.0.0.0:8799, upstream = ollama, model = qwythos
Then in OCLI:
    /connect http://<this-host>:8799     # add --token <t> if MPC_TOKEN is set

Config (env):
    MPC_HOST          bind host           (default 0.0.0.0)
    MPC_PORT          bind port           (default 8799)
    MPC_TOKEN         optional bearer token clients must send
    MPC_UPSTREAM_URL  OpenAI-compatible base url
                      (default http://localhost:11434/v1  = ollama;
                       for llama-server use http://localhost:8080/v1)
    MPC_UPSTREAM_KEY  upstream api key    (default "ollama"; llama-server ignores it)
    MPC_MODEL         default model       (default qwythos)
    MPC_BACKEND       reported backend    (default llama-cpp)
    MPC_MAX_TOKENS    per-reply cap       (default 4096)
"""
import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERSION = "ocli-mpc/1.0"

HOST = os.getenv("MPC_HOST", "0.0.0.0")
PORT = int(os.getenv("MPC_PORT", "8799"))
TOKEN = os.getenv("MPC_TOKEN", "").strip()
UPSTREAM_URL = os.getenv("MPC_UPSTREAM_URL", "http://localhost:11434/v1")
UPSTREAM_KEY = os.getenv("MPC_UPSTREAM_KEY", "ollama")
DEFAULT_MODEL = os.getenv("MPC_MODEL", "qwythos")
DEFAULT_BACKEND = os.getenv("MPC_BACKEND", "llama-cpp")
MAX_TOKENS = int(os.getenv("MPC_MAX_TOKENS", "4096"))

_state_lock = threading.Lock()
STATE = {"backend": DEFAULT_BACKEND, "model": DEFAULT_MODEL}


def _selected():
    with _state_lock:
        return STATE["backend"], STATE["model"]


def _client():
    from openai import OpenAI
    return OpenAI(base_url=UPSTREAM_URL, api_key=UPSTREAM_KEY or "none")


def _openai_messages(messages):
    """Pass OCLI's role/content messages straight through; coerce tool rows to user."""
    out = []
    for m in messages or []:
        role = m.get("role", "user")
        content = m.get("content", "") or ""
        if role == "tool":
            out.append({"role": "user",
                        "content": "Tool result from %s:\n%s" % (m.get("name", "tool"), content)})
        elif role in ("system", "user", "assistant"):
            out.append({"role": role, "content": content})
        else:
            out.append({"role": "user", "content": content})
    if not out:
        out = [{"role": "user", "content": ""}]
    return out


def _list_models():
    models = []
    try:
        for m in _client().models.list().data:
            mid = getattr(m, "id", None)
            if mid:
                models.append(mid)
    except Exception:
        pass
    if DEFAULT_MODEL not in models:
        models.insert(0, DEFAULT_MODEL)
    return models


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    # ---- helpers ----
    def _auth_ok(self):
        if not TOKEN:
            return True
        got = self.headers.get("Authorization", "")
        if got.startswith("Bearer "):
            got = got[7:]
        return got.strip() == TOKEN

    def _read_json(self):
        try:
            n = int(self.headers.get("Content-Length", "0") or "0")
        except ValueError:
            n = 0
        raw = self.rfile.read(n) if n > 0 else b""
        if not raw:
            return {}
        try:
            return json.loads(raw.decode("utf-8", "replace"))
        except Exception:
            return {}

    def _send_json(self, obj, code=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except Exception:
            pass

    def _begin_ndjson(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()

    def _emit(self, event):
        try:
            self.wfile.write((json.dumps(event) + "\n").encode("utf-8"))
            self.wfile.flush()
            return True
        except Exception:
            return False

    def _guard(self):
        if not self._auth_ok():
            self._send_json({"error": "unauthorized"}, 401)
            return False
        return True

    # ---- routing ----
    def do_GET(self):
        if not self._guard():
            return
        path = self.path.split("?", 1)[0]
        if path == "/status" or path == "/":
            bk, md = _selected()
            self._send_json({"ok": True, "version": VERSION,
                             "selected_backend": bk, "selected_model": md})
        elif path == "/models":
            bk, md = _selected()
            models = _list_models()
            self._send_json({"backend": bk, "models": models, "installed": models,
                             "selected_backend": bk, "selected_model": md})
        elif path == "/downloads":
            self._send_json({"jobs": []})
        elif path.startswith("/downloads/"):
            self._send_json({"id": path.rsplit("/", 1)[-1], "status": "unsupported", "jobs": []})
        else:
            self._send_json({"error": "not found: " + path}, 404)

    def do_POST(self):
        if not self._guard():
            return
        path = self.path.split("?", 1)[0]
        payload = self._read_json()
        if path == "/chat":
            self._chat(payload)
        elif path == "/select":
            bk = payload.get("backend") or DEFAULT_BACKEND
            md = payload.get("model") or DEFAULT_MODEL
            with _state_lock:
                STATE["backend"], STATE["model"] = bk, md
            self._send_json({"selected_backend": bk, "selected_model": md})
        elif path in ("/download", "/cancel", "/delete"):
            # model management is done out-of-band (install_qwythos.sh / ollama); acknowledge.
            self._send_json({"ok": True, "status": "unsupported",
                             "message": "manage models with install_qwythos.sh or ollama"})
        else:
            self._send_json({"error": "not found: " + path}, 404)

    # ---- chat ----
    def _chat(self, payload):
        bk, md = _selected()
        model = payload.get("model") or md
        messages = _openai_messages(payload.get("messages"))
        stream = bool(payload.get("stream"))
        if stream:
            self._chat_stream(model, bk, messages)
        else:
            self._chat_once(model, bk, messages)

    def _chat_once(self, model, backend, messages):
        try:
            client = _client()
            resp = client.chat.completions.create(
                model=model, messages=messages, max_tokens=MAX_TOKENS, stream=False)
            content = ""
            if resp.choices:
                content = getattr(resp.choices[0].message, "content", "") or ""
            self._send_json({"content": content, "backend": backend, "model": model,
                             "selected_backend": backend, "selected_model": model,
                             "metadata": {"mpc": True, "upstream": UPSTREAM_URL}})
        except Exception as e:
            self._send_json({"error": "upstream error: " + str(e)}, 502)

    def _chat_stream(self, model, backend, messages):
        try:
            client = _client()
            completion = client.chat.completions.create(
                model=model, messages=messages, max_tokens=MAX_TOKENS, stream=True)
        except Exception as e:
            self._begin_ndjson()
            self._emit({"type": "error", "error": "upstream error: " + str(e), "retryable": True})
            return
        self._begin_ndjson()
        in_thought = False
        try:
            for chunk in completion:
                if not getattr(chunk, "choices", None):
                    continue
                delta = getattr(chunk.choices[0], "delta", None)
                if delta is None:
                    continue
                reasoning = getattr(delta, "reasoning_content", None)
                if reasoning:
                    if not in_thought:
                        if not self._emit({"type": "content", "content": "<thought>"}):
                            return
                        in_thought = True
                    if not self._emit({"type": "content", "content": reasoning}):
                        return
                text = getattr(delta, "content", None)
                if text:
                    if in_thought:
                        self._emit({"type": "content", "content": "</thought>\n"})
                        in_thought = False
                    if not self._emit({"type": "content", "content": text}):
                        return
            if in_thought:
                self._emit({"type": "content", "content": "</thought>\n"})
            self._emit({"type": "metadata", "metadata": {"mpc": True, "upstream": UPSTREAM_URL}})
            self._emit({"type": "done", "selected_backend": backend, "selected_model": model})
        except Exception as e:
            self._emit({"type": "error", "error": "stream error: " + str(e), "retryable": True})


def main():
    try:
        import openai  # noqa: F401
    except ImportError:
        sys.stderr.write("The 'openai' package is required: pip install openai\n")
        sys.exit(1)
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    print("OCLI MPC server %s on http://%s:%d" % (VERSION, HOST, PORT))
    print("  upstream: %s   default model: %s (%s)" % (UPSTREAM_URL, DEFAULT_MODEL, DEFAULT_BACKEND))
    print("  connect from OCLI:  /connect http://%s:%d%s" %
          ("<host>" if HOST == "0.0.0.0" else HOST, PORT, "  --token <t>" if TOKEN else ""))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down")
        httpd.shutdown()


if __name__ == "__main__":
    main()
