#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <cmake-build-directory>" >&2
  exit 2
fi

if [[ $(uname -s) != Linux ]]; then
  echo "error: privileged Linux sandbox integration requires Linux" >&2
  exit 1
fi

cgroup_root=/sys/fs/cgroup
if [[ ! -f "${cgroup_root}/cgroup.controllers" ]]; then
  echo "error: cgroup v2 is not mounted at ${cgroup_root}" >&2
  exit 1
fi

for controller in memory pids; do
  if ! grep -qw "${controller}" "${cgroup_root}/cgroup.controllers"; then
    echo "error: cgroup v2 controller is unavailable: ${controller}" >&2
    exit 1
  fi
done

if ((EUID == 0)); then
  privileged=()
else
  if ! command -v sudo >/dev/null 2>&1; then
    echo "error: sudo is required to provision a delegated cgroup" >&2
    exit 1
  fi
  privileged=(sudo)
fi

apparmor_userns_path=/proc/sys/kernel/apparmor_restrict_unprivileged_userns
unprivileged_userns_path=/proc/sys/kernel/unprivileged_userns_clone
apparmor_userns_original=
unprivileged_userns_original=
cgroup_parent="${cgroup_root}/zom-linux-sandbox-${UID}-$$"

cleanup() {
  local status=$?
  local cleanup_status=0
  trap - EXIT
  set +e
  if [[ -d "${cgroup_parent}/isolated-run" ]]; then
    if [[ -w "${cgroup_parent}/isolated-run/cgroup.kill" ]]; then
      printf '1\n' >"${cgroup_parent}/isolated-run/cgroup.kill"
    else
      printf '1\n' | "${privileged[@]}" tee \
        "${cgroup_parent}/isolated-run/cgroup.kill" >/dev/null
    fi
    "${privileged[@]}" rmdir "${cgroup_parent}/isolated-run" || cleanup_status=$?
  fi
  if [[ -d "${cgroup_parent}" ]]; then
    "${privileged[@]}" rmdir "${cgroup_parent}" || cleanup_status=$?
  fi
  if [[ -n ${unprivileged_userns_original} ]]; then
    printf '%s\n' "${unprivileged_userns_original}" | "${privileged[@]}" tee \
      "${unprivileged_userns_path}" >/dev/null || cleanup_status=$?
  fi
  if [[ -n ${apparmor_userns_original} ]]; then
    printf '%s\n' "${apparmor_userns_original}" | "${privileged[@]}" tee \
      "${apparmor_userns_path}" >/dev/null || cleanup_status=$?
  fi
  if ((status != 0)); then
    exit "${status}"
  fi
  exit "${cleanup_status}"
}
trap cleanup EXIT

if ((EUID != 0)); then
  if [[ -r "${apparmor_userns_path}" ]]; then
    apparmor_userns_original=$(<"${apparmor_userns_path}")
    if [[ ${apparmor_userns_original} != 0 ]]; then
      printf '0\n' | "${privileged[@]}" tee "${apparmor_userns_path}" >/dev/null
    fi
  fi
  if [[ -r "${unprivileged_userns_path}" ]]; then
    unprivileged_userns_original=$(<"${unprivileged_userns_path}")
    if [[ ${unprivileged_userns_original} != 1 ]]; then
      printf '1\n' | "${privileged[@]}" tee "${unprivileged_userns_path}" >/dev/null
    fi
  fi
fi

if ! unshare --user --map-root-user true; then
  echo "error: unprivileged user namespaces remain unavailable after host provisioning" >&2
  exit 1
fi

"${privileged[@]}" mkdir "${cgroup_parent}"
for controller in memory pids; do
  if ! grep -qw "${controller}" "${cgroup_parent}/cgroup.controllers"; then
    echo "error: cgroup v2 controller was not delegated: ${controller}" >&2
    exit 1
  fi
done
printf '+memory +pids\n' | "${privileged[@]}" tee \
  "${cgroup_parent}/cgroup.subtree_control" >/dev/null
"${privileged[@]}" chown "${UID}:$(id -g)" \
  "${cgroup_parent}" \
  "${cgroup_parent}/cgroup.procs" \
  "${cgroup_parent}/cgroup.subtree_control"
if [[ -f "${cgroup_parent}/cgroup.threads" ]]; then
  "${privileged[@]}" chown "${UID}:$(id -g)" "${cgroup_parent}/cgroup.threads"
fi

test_command=(
  ctest --test-dir "$1"
  -R '^linux-native-sandbox-integration$'
  --output-on-failure --verbose
)
if [[ -n ${ZOM_LINUX_SANDBOX_TRACE_PREFIX:-} ]]; then
  ZOM_LINUX_SANDBOX_CGROUP_PARENT="${cgroup_parent}" \
    strace -ff -o "${ZOM_LINUX_SANDBOX_TRACE_PREFIX}" "${test_command[@]}"
else
  ZOM_LINUX_SANDBOX_CGROUP_PARENT="${cgroup_parent}" "${test_command[@]}"
fi
