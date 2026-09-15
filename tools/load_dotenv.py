"""Inject API_BASE / API_HOST / API_TOKEN from .env into the firmware build."""

Import("env")  # noqa: F821 -- injected by SCons

import os
import sys

project_dir = env["PROJECT_DIR"]  # noqa: F821
sys.path.insert(0, os.path.join(project_dir, "tools"))
from envutil import load_api_config

# Firmware secrets come only from `.env`, never from the committed example.
cfg = load_api_config(project_dir, include_example=False)

if not cfg["base"] or not cfg["host"]:
    sys.stderr.write(
        "\nAPI_BASE is missing or invalid.\n"
        "Copy .env.example to .env and set API_BASE, then rebuild.\n\n"
    )
    raise SystemExit(1)

env.Append(  # noqa: F821
    CPPDEFINES=[
        ("API_BASE", env.StringifyMacro(cfg["base"])),  # noqa: F821
        ("API_HOST", env.StringifyMacro(cfg["host"])),  # noqa: F821
        ("API_TOKEN", env.StringifyMacro(cfg["token"])),  # noqa: F821
    ]
)
