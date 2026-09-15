"""Shared .env loader for the firmware build and the Python tools."""

from pathlib import Path
from urllib.parse import urlparse


def parse_env_file(path):
    values = {}
    text = Path(path).read_text(encoding="utf-8-sig")
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].strip()
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    return values


def load_project_env(root, include_example=True):
    """Load `.env`. Optionally seed from `.env.example` first (docs / tools)."""
    root = Path(root)
    values = {}
    if include_example:
        example = root / ".env.example"
        if example.is_file():
            values.update(parse_env_file(example))
    dotenv = root / ".env"
    if dotenv.is_file():
        values.update(parse_env_file(dotenv))
    return values


def load_api_config(root, include_example=True):
    values = load_project_env(root, include_example=include_example)
    base = (values.get("API_BASE") or "").strip().rstrip("/")
    host = (values.get("API_HOST") or "").strip()
    token = values.get("API_TOKEN") or ""
    if not host and base:
        host = urlparse(base).hostname or ""
    return {"base": base, "host": host, "token": token}
