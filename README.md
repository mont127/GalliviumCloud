# OCLI

An autonomous, **local-first AI coding, research, and security agent** for your terminal.

OCLI runs a model of your choice — a local GGUF/MLX model or a cloud API — inside a
real agent loop with real tools: shell, files, git, web search, and a shared
terminal that both you and the AI type into. It ships as a single C++20 binary and
presents a live split-pane TUI plus an optional LAN web dashboard.

It is tuned as an **ethical red-team / security assistant** as well as a general
coding agent.

---

## Highlights

- **Split-pane live view** — conversation on the left, a shared PTY terminal on the right that both you and the AI drive.
- **Many backends** — local (`llama.cpp`, `MLX`, `Ollama`), cloud (`OpenAI`, `Anthropic`, `NVIDIA NIM`), or a remote **MPC** server you connect to.
- **Qwythos-9B (1M context)** as the default local model, with a one-command installer.
- **Context-window progress bar** and **effort levels** that scale how long the model thinks before it acts; automatic history compaction.
- **LAN web dashboard**, parallel **worker sub-agents**, and an autonomous **`/loop`** mode.
- **Portable sharing** — run the model server in Docker and get a public Cloudflare-tunnel URL any OCLI can connect to.

---

## Requirements

- macOS or Linux
- A **C++20** compiler, **CMake**, and **libcurl** (+ pthreads)
  - macOS: `xcode-select --install` then `brew install cmake curl`
  - Debian/Ubuntu: `sudo apt install build-essential cmake libcurl4-openssl-dev`
- **Python 3** for the helper bridges (search / NVIDIA / MPC)
- At least one model backend (see [Backends](#backends--models))

## Build & install

```bash
git clone https://github.com/mont127/GalliviumCloud.git
cd GalliviumCloud
make            # builds ./build/ocli
make install    # installs to ~/bin/ocli  (+ helper scripts alongside it)
```

Make sure `~/bin` is on your `PATH`, then run `ocli`.

---

## Quick start (tutorial)

**1. Get a model.** The fastest path is the bundled default, Qwythos:

```bash
install_qwythos.sh        # downloads the GGUF to ~/models and registers it with ollama
```

(or point OCLI at any [backend](#backends--models) below).

**2. Run it.**

```bash
ocli
```

You land in the **split-pane live view**: type a prompt in the bar at the bottom,
watch the AI answer on the left and work in the terminal on the right.

**3. Pick a backend/model** if needed:

```
/backend llama-cpp        # or: ollama, openai, anthropic, nvidia, mlx
/model                    # arrow-select a model
```

**4. Give it real work** — it uses tools and carries the task through to a result:

```
scan this repo for a path-traversal bug and write a failing test that proves it
```

### Using the live view

- Type in the bottom bar to chat; **Enter** sends.
- **Ctrl-T** toggles focus into the right-hand shared terminal so you can run commands yourself — the AI sees what you run, and its output can be sent along with your next message.
- **Mouse wheel** or **PgUp/PgDn** scrolls the conversation (and the terminal pane).
- The pill shows a live **context-window bar** (`ctx ▰▰▱▱ NN%`).
- **Double-Ctrl-C** quits. `/classic` switches to a plain scrolling view (or start with `LOREA_CLASSIC=1 ocli`).
- `/terminal` attaches full-screen to the shared shell (Ctrl-] to detach).

### The web dashboard

```
/dashboard
```

Opens a dark, LAN-reachable web UI (JetBrains Mono) with the same conversation plus a
live `xterm.js` terminal. Open the printed URL from any device on your network.

> The dashboard runs arbitrary shell commands. Only use it on a trusted network.

---

## Backends & models

| Backend | What it is | Setup |
|---|---|---|
| `llama-cpp` | Local GGUF via a `llama-server` | `/setup` installs llama.cpp; default model = Qwythos (`install_qwythos.sh`) |
| `ollama` | Local Ollama | run ollama; `install_qwythos.sh` registers `qwythos` |
| `mlx` | Apple-Silicon MLX server | `/setup_mlx` |
| `openai` | OpenAI API | `export OPENAI_API_KEY=...` |
| `anthropic` | Claude API | `export ANTHROPIC_API_KEY=...` |
| `nvidia` | NVIDIA NIM (OpenAI-compatible) via a Python bridge | key in `~/.config/lorea/nvidia_api_key` or `NVIDIA_API_KEY` |
| MPC | A remote OCLI model server you connect to | `/connect <url> [--token t]` |

Switch with `/backend <name>` and pick a model with `/model`. `llama-cpp` and `ollama`
default to **Qwythos** once the GGUF is installed.

### Context window

The working context is model-aware; Qwythos gets a large window. Override it (bounded by your RAM):

```bash
LOREA_CONTEXT=128k ocli     # also 256k, 1M, ...
```

### Effort / thinking

`/effort` sets how hard the model works — and, crucially, **how much it thinks before it
acts**. It ranges from `basic` (answer directly) through `elite` and `mythic` to
`beyond` (deliberate to the maximum inside its reasoning before committing).

---

## Share a model over the internet (MPC + Docker + Cloudflare)

Serve any model over OCLI's MPC protocol and get a public URL, from any machine with Docker:

```bash
cd mcp-docker
docker compose up --build
```

On first start it downloads/serves Qwythos and prints:

```
==================================================================
  OCLI MPC is LIVE at:  https://<name>.trycloudflare.com
  Connect from any OCLI:
      /connect https://<name>.trycloudflare.com --token <token>
==================================================================
```

Paste that `/connect …` line into any OCLI. To use a faster **native** model (your GPU)
instead of the CPU container, point the proxy at it:

```bash
MPC_UPSTREAM_URL=http://host.docker.internal:11434/v1 docker compose up --build mcp
```

You can also run the MPC server without Docker: `mcpserver.py` (needs the `openai`
package and an OpenAI-compatible upstream). See `mcp-docker/README.md` for details.

---

## Command reference

**Session:** `/status` · `/usage` (token + context + activity stats) · `/tasks` · `/save [name]` · `/load [name]` · `/sessions` · `/exit`

**Workspace:** `/cmd <shell>` (run without using a turn) · `/diff [path]` · `/copy` · `/retry` · `/undo` · `/clear`

**View & terminal:** `/live` · `/classic` · `/terminal` · `/dashboard`

**Agents:** `/agent [n] [goal]` · `/agent --full [n] [goal]` (agents may run commands / edit files)

**Models & backends:** `/backend` · `/model` · `/connect [url] [--token t]` · `/setup` (llama.cpp) · `/setup_mlx` · `/download <repo>` · `/download_model [m]`

**Behavior:** `/auto` (auto-execute) · `/effort` · `/loop <goal>` (autonomous; Esc stops) · `/plan` · `/vram [--auto]` · `/theme [name]` · `/help`

---

## Config (environment)

| Variable | Effect |
|---|---|
| `LOREA_CLASSIC=1` | start in the plain scrolling view (no split-pane) |
| `LOREA_CONTEXT=128k` | set the working context window (`128k`, `256k`, `1M`, …) |
| `NVIDIA_API_KEY` | key for the `nvidia` backend (or use `~/.config/lorea/nvidia_api_key`) |
| `OPENAI_API_KEY` / `ANTHROPIC_API_KEY` | cloud backend keys |
| `PYTHON` | python interpreter for the helper bridges |
| `BINDIR` (make) | install directory (default `~/bin`) |

---

## Ethical use

OCLI is a security assistant built for **authorized, lawful work** — your own systems,
engagements you are hired for, CTFs, labs, security research, and education. Use it only
where you have permission. It refuses and redirects clearly unauthorized or mass-harm
requests.

## License

See [LICENSE](LICENSE).
