"""Make sure esptool's own dependencies are present before the build.

The esptool shipped with tool-esptoolpy 4.9 imports `intelhex`, but PlatformIO
does not install it into its virtualenv. The build then dies with

    ModuleNotFoundError: No module named 'intelhex'

while linking bootloader.bin. Installing it by hand works until PlatformIO
upgrades its core and rebuilds the virtualenv, which silently drops the
package again -- so the check belongs in the build.
"""

import subprocess
import sys

Import("env")  # noqa: F821 -- injected by SCons

REQUIRED = ["intelhex"]


def ensure(module):
    try:
        __import__(module)
        return
    except ImportError:
        pass

    # stderr: SCons buffers the pre-script's stdout, so a plain print would
    # not show up in the build output.
    sys.stderr.write(f"installing missing esptool dependency: {module}\n")
    try:
        subprocess.check_call(
            [sys.executable, "-m", "pip", "install", "--quiet", module],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(
            f"\nCould not install '{module}' into {sys.executable}.\n"
            f"Install it manually and rebuild:\n"
            f'  "{sys.executable}" -m pip install {module}\n\n'
        )
        raise SystemExit(exc.returncode)


for module in REQUIRED:
    ensure(module)
