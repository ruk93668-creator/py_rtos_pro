"""Build the proposed integration in a temporary copy; never patch the project."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET


PROJECT_ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_DIRS = ("App", "Service", "BSP", "Driver", "core", "Platform", "Middlewares", "MDK-ARM")
PROTECTED_DIRS = ("BSP", "Driver", "core", "Platform", "Middlewares", "MDK-ARM")


def protected_hashes() -> dict[str, str]:
    return {
        path.relative_to(PROJECT_ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for directory in PROTECTED_DIRS
        for path in (PROJECT_ROOT / directory).rglob("*")
        if path.is_file()
    }


def hidden_process_options() -> dict:
    if os.name != "nt":
        return {}
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    return {"startupinfo": startup, "creationflags": subprocess.CREATE_NO_WINDOW}


def decode_log(path: Path) -> str:
    if not path.exists():
        return ""
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    for encoding in ("utf-8-sig", "gbk"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            pass
    return data.decode("utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uv4", type=Path, default=Path(r"D:\32\UV4\UV4.exe"))
    parser.add_argument("--patch", type=Path, default=PROJECT_ROOT / "Docs/proposed_low_level.patch")
    parser.add_argument("--report", type=Path, default=PROJECT_ROOT / "Tests/out/proposed_firmware_build.json")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    if not args.uv4.is_file():
        parser.error("uVision executable not found: " + str(args.uv4))
    if not args.patch.is_file():
        parser.error("Proposal patch not found: " + str(args.patch))
    git = shutil.which("git")
    if not git:
        parser.error("git is required to apply the proposal to the temporary copy")

    before = protected_hashes()
    staging = Path(tempfile.mkdtemp(prefix="py_rtos_pro_proposed_")).resolve()
    # A newly generated TEMP directory is the sole destination of patch/build writes.
    if staging == PROJECT_ROOT or PROJECT_ROOT in staging.parents:
        raise RuntimeError("Staging must be outside the original project")
    report = {"staging": str(staging), "original_project": str(PROJECT_ROOT), "success": False}
    started = time.monotonic()
    result_code = 1
    try:
        for directory in FIRMWARE_DIRS:
            shutil.copytree(PROJECT_ROOT / directory, staging / directory,
                            ignore=shutil.ignore_patterns("Objects", "Listings", ".git", "*.uvguix.*"))
        for action in (["--check"], []):
            # Keep the source tree's existing LF/CRLF and legacy byte
            # encodings intact while applying the review-only proposal.
            subprocess.run([git, "-c", "core.autocrlf=false", "apply", *action,
                            str(args.patch.resolve())], cwd=staging,
                           check=True, capture_output=True, **hidden_process_options())

        project = staging / "MDK-ARM/yxj.uvprojx"
        xml = ET.parse(project)
        target = xml.find("./Targets/Target")
        if target is None:
            raise RuntimeError("No MDK target found")
        # Custom build hooks could contain absolute writes. This project has none enabled.
        for element in target.findall("./TargetOption/TargetCommonOption/*/*"):
            if element.tag.startswith("RunUserProg") and element.text == "1":
                raise RuntimeError("Enabled custom build hook requires separate inspection")
        target_name = target.findtext("TargetName")
        common = target.find("./TargetOption/TargetCommonOption")
        output_dir = common.findtext("OutputDirectory", ".\\Objects\\")
        output_name = common.findtext("OutputName", "yxj")
        executable = (project.parent / output_dir / (output_name + ".axf")).resolve()
        if staging not in executable.parents:
            raise RuntimeError("Configured build output is outside the temporary directory")
        build_log = staging / "proposed_firmware_build.log"
        command = [str(args.uv4.resolve()), "-b", str(project), "-t", target_name, "-o", str(build_log)]
        print("Building proposed firmware in " + str(staging), flush=True)
        completed = subprocess.run(command, cwd=project.parent, timeout=args.timeout,
                                   capture_output=True, **hidden_process_options())
        log_text = decode_log(build_log)
        success = completed.returncode in (0, 1) and executable.is_file()
        report.update({"target": target_name, "uv4_return_code": completed.returncode,
                       "build_log": str(build_log), "build_output": log_text,
                       "executable": str(executable) if executable.exists() else None,
                       "success": success})
        print(log_text, flush=True)
        result_code = 0 if success else 1
    except Exception as error:
        report["error"] = str(error)
        if isinstance(error, subprocess.CalledProcessError):
            report["error_output"] = error.stderr.decode("utf-8", errors="replace")
        print("Proposed build failed: " + str(error), flush=True)
    finally:
        after = protected_hashes()
        unchanged = before == after
        report["protected_file_count"] = len(before)
        report["protected_files_unchanged"] = unchanged
        report["elapsed_seconds"] = round(time.monotonic() - started, 2)
        if not unchanged:
            report["success"] = False
            report["changed_protected_files"] = [key for key in before.keys() | after.keys()
                                                   if before.get(key) != after.get(key)]
            result_code = 1
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print("Report: " + str(args.report.resolve()), flush=True)
        print("Original protected files unchanged: " + str(unchanged), flush=True)
    return result_code


if __name__ == "__main__":
    raise SystemExit(main())
