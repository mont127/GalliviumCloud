#!/usr/bin/env bash
# Install the Qwythos-9B (1M context) GGUF for OCLI's llama.cpp (and ollama) backend.
# Quant override:  QWYTHOS_QUANT=Q6_K ./install_qwythos.sh   (Q4_K_M default; also Q5_K_M, Q6_K, Q8_0)
set -euo pipefail

REPO="empero-ai/Qwythos-9B-Claude-Mythos-5-1M-GGUF"
QUANT="${QWYTHOS_QUANT:-Q4_K_M}"
FILE="Qwythos-9B-Claude-Mythos-5-1M-${QUANT}.gguf"
DEST_DIR="$HOME/models"
DEST="$DEST_DIR/$FILE"
DEFAULT="$DEST_DIR/Qwythos-9B-Claude-Mythos-5-1M-Q4_K_M.gguf"   # path OCLI's default resolves to
URL="https://huggingface.co/${REPO}/resolve/main/${FILE}?download=true"

mkdir -p "$DEST_DIR"

if [ -f "$DEST" ]; then
  echo "Already present: $DEST ($(du -h "$DEST" | cut -f1))"
else
  echo "Downloading $FILE  (~several GB) -> $DEST"
  if command -v huggingface-cli >/dev/null 2>&1; then
    huggingface-cli download "$REPO" "$FILE" --local-dir "$DEST_DIR" --local-dir-use-symlinks False
  else
    # resumable (-C -) so a dropped multi-GB download continues where it left off
    curl -L --fail --retry 5 --retry-delay 5 -C - -o "$DEST" "$URL"
  fi
  echo "Downloaded: $DEST ($(du -h "$DEST" | cut -f1))"
fi

# make the CLI's expected default path resolve even if a non-Q4_K_M quant was fetched
if [ "$DEST" != "$DEFAULT" ]; then
  ln -sf "$DEST" "$DEFAULT"
  echo "Linked default -> $DEST"
fi

# register with ollama (if installed) so `/backend ollama` default 'qwythos' works
if command -v ollama >/dev/null 2>&1; then
  MF="$(mktemp)"
  printf 'FROM %s\nPARAMETER num_ctx 32768\n' "$DEST" > "$MF"
  echo "Registering ollama model 'qwythos' ..."
  if ollama create qwythos -f "$MF"; then echo "ollama model 'qwythos' ready."; else echo "ollama create failed (skipped)."; fi
  rm -f "$MF"
else
  echo "ollama not found — skipping ollama registration (llama-cpp is set up regardless)."
fi

echo
echo "Done. In ocli:"
echo "  /backend llama-cpp    -> defaults to $DEFAULT"
echo "  /backend ollama       -> defaults to 'qwythos'"
echo "  (needs llama-server on PATH for the llama-cpp backend; run /setup inside ocli if missing.)"
