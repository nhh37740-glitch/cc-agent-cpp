#!/usr/bin/env bash
set -Eeuo pipefail

build_number="${1:-${BUILD_NUMBER:-}}"
if [[ ! "$build_number" =~ ^[0-9]+$ ]]; then
  echo "Usage: BUILD_NUMBER=<numeric> bash scripts/deploy-demo-linux.sh [build-number]" >&2
  exit 2
fi

docker=(sudo docker)
image="cc-agent-cpp:${build_number}"
container="cc-agent-cpp-demo"
backup="${container}-rollback-${build_number}"
host_port=18103
url="http://127.0.0.1:${host_port}"

"${docker[@]}" image inspect "$image" >/dev/null
if "${docker[@]}" container inspect "$backup" >/dev/null 2>&1; then
  echo "Refusing to overwrite existing rollback container: $backup" >&2
  exit 1
fi

had_old=false
old_stopped=false
old_renamed=false
deploy_succeeded=false

rollback_on_failure() {
  status=$?
  trap - EXIT
  if [[ "$status" -ne 0 && "$deploy_succeeded" != true ]]; then
    if "${docker[@]}" container inspect "$container" >/dev/null 2>&1; then
      "${docker[@]}" rm --force "$container" >/dev/null 2>&1 || true
    fi
    if [[ "$old_renamed" == true ]]; then
      if "${docker[@]}" rename "$backup" "$container" &&
         "${docker[@]}" start "$container" >/dev/null; then
        echo "Restored previous demo container: $container" >&2
      else
        echo "ERROR: previous container is retained as $backup but could not be restarted" >&2
      fi
    elif [[ "$old_stopped" == true && "$had_old" == true ]]; then
      "${docker[@]}" start "$container" >/dev/null 2>&1 ||
        echo "ERROR: previous demo container $container could not be restarted" >&2
    fi
  fi
  exit "$status"
}
trap rollback_on_failure EXIT

if "${docker[@]}" container inspect "$container" >/dev/null 2>&1; then
  old_role="$("${docker[@]}" inspect --format '{{ index .Config.Labels "cc-agent-cpp.role" }}' "$container")"
  old_binds="$("${docker[@]}" inspect --format '{{json .HostConfig.Binds}}' "$container")"
  if [[ "$old_role" != "waiting-demo" ]] ||
     [[ "$old_binds" != "null" && "$old_binds" != "[]" ]]; then
    echo "Refusing to replace an unmanaged or mounted container named $container" >&2
    exit 1
  fi
  had_old=true
  "${docker[@]}" stop --time 10 "$container" >/dev/null
  old_stopped=true
  "${docker[@]}" rename "$container" "$backup"
  old_renamed=true
fi

# This demo intentionally has no bind mounts or volumes. The image's default
# command only serves the truthful waiting_config page; no model/video input is
# present in this container definition.
"${docker[@]}" run --detach \
  --name "$container" \
  --label cc-agent-cpp.role=waiting-demo \
  --label "cc-agent-cpp.build=${build_number}" \
  --publish "127.0.0.1:${host_port}:8080" \
  --restart unless-stopped \
  --user 10001:10001 \
  --read-only \
  --tmpfs /tmp:rw,nosuid,nodev,noexec,size=16m \
  --cap-drop ALL \
  --security-opt no-new-privileges:true \
  --memory 256m \
  --memory-swap 256m \
  --cpus 0.50 \
  --pids-limit 64 \
  "$image" >/dev/null

python3 - "$container" <<'PY'
import json
import subprocess
import sys

raw = subprocess.check_output(["sudo", "docker", "inspect", sys.argv[1]], text=True)
container = json.loads(raw)[0]
config = container["Config"]
host = container["HostConfig"]
assert config["User"] == "10001:10001", config
assert host["ReadonlyRootfs"] is True, host
assert "ALL" in host["CapDrop"], host
assert host["SecurityOpt"] and "no-new-privileges:true" in host["SecurityOpt"], host
assert host["Memory"] == 256 * 1024 * 1024, host
assert host["MemorySwap"] == 256 * 1024 * 1024, host
assert host["NanoCpus"] == 500_000_000, host
assert host["PidsLimit"] == 64, host
assert host["Binds"] in (None, []), host
engine_mounts = {"/etc/hosts", "/etc/hostname", "/etc/resolv.conf"}
for mount in container["Mounts"]:
    if mount["Type"] == "tmpfs":
        assert mount["Destination"] in {"/tmp", "/dev/shm"}, mount
    else:
        assert mount["Type"] == "bind" and mount["Destination"] in engine_mounts, mount
bindings = host["PortBindings"]["8080/tcp"]
assert bindings == [{"HostIp": "127.0.0.1", "HostPort": "18103"}], bindings
print("Container isolation policy and loopback-only publishing verified.")
PY

python3 - "$url" <<'PY'
import json
import sys
import time
import urllib.error
import urllib.request

base = sys.argv[1]
last_error = None
for _ in range(30):
    try:
        with urllib.request.urlopen(base + "/healthz", timeout=2) as response:
            health = response.read().decode("utf-8")
        if health.strip() != "ok":
            raise RuntimeError(f"unexpected /healthz body: {health!r}")
        with urllib.request.urlopen(base + "/api/state", timeout=2) as response:
            state = json.load(response)
        if state.get("run_state") != "waiting_config":
            raise RuntimeError(f"unexpected run_state: {state!r}")
        if state.get("events") != []:
            raise RuntimeError(f"demo must start with no events: {state!r}")
        if "未运行推理" not in state.get("run_detail", ""):
            raise RuntimeError(f"missing truthful idle detail: {state!r}")
        print("Waiting dashboard demo passed /healthz and /api/state checks.")
        break
    except (OSError, urllib.error.URLError, ValueError, RuntimeError) as error:
        last_error = error
        time.sleep(1)
else:
    raise SystemExit(f"Demo smoke check failed: {last_error}")
PY

deploy_succeeded=true
if [[ "$old_renamed" == true ]]; then
  if ! "${docker[@]}" rm --force "$backup" >/dev/null; then
    echo "Warning: new demo is healthy; old rollback container remains as $backup" >&2
  fi
fi
echo "C++ waiting dashboard is available at ${url}/"
