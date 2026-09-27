#!/usr/bin/env python3
"""Package Linux runtime and module archives without bundling private assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import tarfile
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_MODULES = (
    "edge_video",
    "edge_filter",
    "edge_model",
    "edge_skill",
    "edge_agent_core",
    "edge_dashboard",
    "edge_pipeline",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def copy_headers(stage: Path) -> None:
    include = stage / "include"
    shutil.copytree(ROOT / "include", include / "edge")
    shutil.copytree(ROOT / "third_party" / "nlohmann", include / "nlohmann")
    llama = ROOT / "third_party" / "llama.cpp"
    shutil.copytree(llama / "include", include / "llama")
    shutil.copytree(llama / "tools" / "mtmd", include / "mtmd",
                    ignore=shutil.ignore_patterns("*.cpp", "CMakeLists.txt", "README.md"))
    shutil.copytree(llama / "ggml" / "include", include / "ggml")


def copy_licenses(stage: Path) -> None:
    llama_license = ROOT / "third_party" / "llama.cpp" / "LICENSE"
    if not llama_license.is_file():
        raise SystemExit(
            "Pinned llama.cpp LICENSE is missing; initialize the submodule before packaging: "
            "git submodule update --init --recursive"
        )
    target = stage / "third-party-licenses" / "llama.cpp"
    target.mkdir(parents=True)
    shutil.copy2(llama_license, target / "LICENSE")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-commit", default="unknown")
    parser.add_argument("--source-tree", default="unknown")
    args = parser.parse_args()

    binary = ROOT / "build" / "src" / "edge_agent"
    if not binary.is_file():
        raise SystemExit(f"Linux executable missing: {binary}")
    if platform.system() != "Linux" or platform.machine().lower() not in ("x86_64", "amd64"):
        raise SystemExit("This package recipe targets Linux x86_64 only")

    module_dir = ROOT / "build" / "src"
    module_archives = {
        name: module_dir / f"lib{name}.a" for name in EXPECTED_MODULES
    }
    missing_modules = [str(path.relative_to(ROOT)) for path in module_archives.values() if not path.is_file()]
    if missing_modules:
        raise SystemExit("Expected modular static libraries are missing: " + ", ".join(missing_modules))

    dist = ROOT / "dist"
    dist.mkdir(parents=True, exist_ok=True)
    archive = dist / f"cc-agent-cpp-{args.version}-linux-x86_64.tar.gz"
    manifest_out = dist / "manifest-linux-x86_64.json"

    with tempfile.TemporaryDirectory(prefix="cc-agent-cpp-linux-", dir=dist) as temp_name:
        stage_parent = Path(temp_name)
        stage = stage_parent / "cc-agent-cpp-linux-x86_64"
        (stage / "bin").mkdir(parents=True)
        (stage / "modules" / "edge").mkdir(parents=True)
        (stage / "modules" / "llama").mkdir(parents=True)
        shutil.copy2(binary, stage / "bin" / "edge_agent")
        for name, path in module_archives.items():
            shutil.copy2(path, stage / "modules" / "edge" / path.name)

        llama_archives = sorted(
            path for path in (ROOT / "build" / "llama.cpp-build").rglob("*.a")
            if path.is_file()
        )
        if not llama_archives:
            raise SystemExit("No static llama.cpp/ggml archives found in build/llama.cpp-build")
        for path in llama_archives:
            relative = path.relative_to(ROOT / "build" / "llama.cpp-build")
            destination = stage / "modules" / "llama" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)

        copy_headers(stage)
        copy_licenses(stage)
        shutil.copytree(ROOT / "skills", stage / "skills")
        (stage / "docs").mkdir()
        shutil.copy2(ROOT / "README.md", stage / "README.md")
        shutil.copy2(ROOT / "docs" / "MODULES.md", stage / "docs" / "MODULES.md")
        shutil.copy2(ROOT / "docs" / "THIRD_PARTY_NOTICES.md", stage / "docs" / "THIRD_PARTY_NOTICES.md")

        files = sorted(path for path in stage.rglob("*") if path.is_file())
        module_list = [
            {"name": name, "archive": f"modules/edge/lib{name}.a"}
            for name in EXPECTED_MODULES
        ]
        manifest = {
            "schemaVersion": 1,
            "project": "cc-agent-cpp",
            "version": args.version,
            "platform": "linux-x86_64",
            "distribution": "Ubuntu 24.04 compatible",
            "sourceCommit": args.source_commit,
            "sourceTree": args.source_tree,
            "runtimeBinary": "bin/edge_agent",
            "modules": module_list,
            "llamaDependencyArchives": [
                str(path.relative_to(stage)).replace("\\", "/")
                for path in sorted((stage / "modules" / "llama").rglob("*.a"))
            ],
            "runtimePackages": [
                "libavformat60", "libavcodec60", "libavutil58", "libswscale7", "libgomp1"
            ],
            "modelsBundled": False,
            "videoFixturesBundled": False,
            "demoDataBundled": False,
            "files": [
                {
                    "path": str(path.relative_to(stage)).replace("\\", "/"),
                    "bytes": path.stat().st_size,
                    "sha256": sha256(path),
                }
                for path in files
            ],
        }
        (stage / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        sums = [f"{sha256(path)}  {path.relative_to(stage).as_posix()}" for path in sorted(stage.rglob("*")) if path.is_file()]
        (stage / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="ascii")

        with tarfile.open(archive, "w:gz") as output:
            output.add(stage, arcname=stage.name)

    (dist / f"{archive.name}.sha256").write_text(
        f"{sha256(archive)}  {archive.name}\n", encoding="ascii"
    )
    manifest_out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Packaged {archive.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
