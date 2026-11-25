# PlatformIO pre-build script: embeds the git revision as FW_GIT_REV so the
# telemetry header records which firmware (and which calibrated floors) a log
# was taken with. Falls back to "unknown" outside a git checkout.
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)
import subprocess

try:
    rev = subprocess.check_output(
        ["git", "describe", "--always", "--dirty"],
        cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL,  # noqa: F821
    ).decode().strip()
except Exception:
    rev = "unknown"

env.Append(CPPDEFINES=[("FW_GIT_REV", env.StringifyMacro(rev))])  # noqa: F821
