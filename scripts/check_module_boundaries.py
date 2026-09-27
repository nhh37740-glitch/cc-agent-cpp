"""Guard the CMake module map and dependency direction."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE_CMAKE = (ROOT / "src" / "CMakeLists.txt").read_text(encoding="utf-8")
MODULES = {
    "edge_video": ["video/ffmpeg_video_source.cpp", "video/realtime_pacing_source.cpp", "video/yuv_to_rgb.cpp"],
    "edge_filter": ["filter/frame_filter.cpp"],
    "edge_model": ["model/llama_backend.cpp", "model/llama_vlm.cpp", "model/llama_text.cpp", "model/tool_decision_model.cpp"],
    "edge_skill": ["skill/skill_loader.cpp"],
    "edge_agent_core": ["agent/structured_output.cpp", "agent/builtin_tools.cpp", "agent/agent.cpp"],
    "edge_dashboard": ["web/dashboard.cpp"],
    "edge_pipeline": ["app/pipeline.cpp"],
}
DEPENDENCIES = {
    "edge_video": [],
    "edge_filter": ["edge_video"],
    "edge_model": [],
    "edge_skill": [],
    "edge_agent_core": ["edge_model", "edge_skill"],
    "edge_dashboard": ["edge_agent_core", "edge_video", "Threads::Threads"],
    "edge_pipeline": ["edge_video", "edge_filter", "edge_model", "edge_skill", "edge_agent_core", "edge_dashboard"],
}

failures = []
for module, sources in MODULES.items():
    match = re.search(rf"add_library\({module} STATIC\s+(.*?)\)", SOURCE_CMAKE, re.S)
    if not match:
        failures.append(f"missing static library target {module}")
        continue
    body = match.group(1)
    for source in sources:
        if source not in body:
            failures.append(f"{module} does not own {source}")
    link = re.search(rf"target_link_libraries\({module} PUBLIC\s+(.*?)\)", SOURCE_CMAKE, re.S)
    linked = set(link.group(1).split()) if link else set()
    missing = set(DEPENDENCIES[module]) - linked
    if missing:
        failures.append(f"{module} is missing dependency edges: {', '.join(sorted(missing))}")

if not re.search(r"add_library\(edge_core INTERFACE\).*?target_link_libraries\(edge_core INTERFACE edge_pipeline\)", SOURCE_CMAKE, re.S):
    failures.append("edge_core compatibility interface must only expose edge_pipeline")

if failures:
    print("C++ module boundary violations:\n" + "\n".join(failures), file=sys.stderr)
    raise SystemExit(1)
print("C++ static module boundaries: OK")
