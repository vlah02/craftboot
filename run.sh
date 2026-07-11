#!/bin/bash
# Dev helper: create a venv, install pygame, run the prototype windowed.
# Falls back to the system python3-pygame package if venv/pip aren't available.
set -euo pipefail
cd "$(dirname "$0")"

if python3 -c "import pygame" 2>/dev/null; then
    exec python3 app/main.py --windowed "$@"
fi

if [[ ! -d .venv ]]; then
    echo "==> Creating virtualenv (.venv) ..."
    if ! python3 -m venv .venv 2>/dev/null; then
        cat <<'EOF'
Could not create a venv. Install the prerequisites first, e.g.:
    sudo apt install python3-venv python3-pip
or install pygame system-wide:
    sudo apt install python3-pygame
Then re-run ./run.sh
EOF
        exit 1
    fi
fi
# shellcheck disable=SC1091
source .venv/bin/activate
python -m pip install --quiet --upgrade pip
python -m pip install --quiet -r requirements.txt
exec python app/main.py --windowed "$@"
