#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_WORKDIR="/Users/caojunze424/.codex/worktrees/f141/SysyCC"
DEFAULT_PROMPT_FILE="${SCRIPT_DIR}/prompts/arm_perf_aggressive_codex_prompt.txt"

CODEX_BIN="${CODEX_BIN:-$(command -v codex || true)}"
WORKDIR="${WORKDIR:-${DEFAULT_WORKDIR}}"
PROMPT_FILE="${PROMPT_FILE:-${DEFAULT_PROMPT_FILE}}"
POLL_INTERVAL_SECONDS="${POLL_INTERVAL_SECONDS:-15}"
MAX_RUNTIME_SECONDS="${MAX_RUNTIME_SECONDS:-28800}"
TERMINAL_APP="${TERMINAL_APP:-Terminal}"
STALE_AFTER_SECONDS="${STALE_AFTER_SECONDS:-900}"
DRY_RUN=0
ONCE=0
RESUME_FIRST=1
RESTART_STALE=0
LOG_FILE="${LOG_FILE:-${TMPDIR:-/tmp}/codex-cli-watchdog.log}"

usage() {
    cat <<'EOF'
Usage:
  scripts/codex_cli_watchdog.sh [options]

Options:
  --workdir PATH        Working directory for the relaunched Codex session
  --prompt-file PATH    Prompt file to feed into a new or resumed Codex session
  --poll-seconds N      Poll interval in seconds
  --max-runtime N       Maximum watchdog runtime in seconds
  --terminal-app NAME   macOS terminal app to use: Terminal or iTerm
  --stale-after N       Mark a Codex session stale if no local activity files changed for N seconds
  --restart-stale       Kill stale Codex sessions and relaunch automatically
  --no-resume           Start a fresh Codex session instead of trying resume --last first
  --once                Run one check only, then exit
  --dry-run             Print actions without launching Codex
  -h, --help            Show help

Environment overrides:
  CODEX_BIN
  WORKDIR
  PROMPT_FILE
  POLL_INTERVAL_SECONDS
  MAX_RUNTIME_SECONDS
  TERMINAL_APP
  STALE_AFTER_SECONDS
  LOG_FILE
EOF
}

require_positive_integer() {
    local value="$1"
    local option_name="$2"
    if [[ ! "${value}" =~ ^[1-9][0-9]*$ ]]; then
        echo "${option_name} must be a positive integer, got '${value}'" >&2
        exit 1
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --workdir)
        WORKDIR="$2"
        shift 2
        ;;
    --prompt-file)
        PROMPT_FILE="$2"
        shift 2
        ;;
    --poll-seconds)
        POLL_INTERVAL_SECONDS="$2"
        shift 2
        ;;
    --max-runtime)
        MAX_RUNTIME_SECONDS="$2"
        shift 2
        ;;
    --terminal-app)
        TERMINAL_APP="$2"
        shift 2
        ;;
    --stale-after)
        STALE_AFTER_SECONDS="$2"
        shift 2
        ;;
    --restart-stale)
        RESTART_STALE=1
        shift
        ;;
    --no-resume)
        RESUME_FIRST=0
        shift
        ;;
    --once)
        ONCE=1
        shift
        ;;
    --dry-run)
        DRY_RUN=1
        shift
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        echo "unknown option: $1" >&2
        usage >&2
        exit 1
        ;;
    esac
done

if [[ -z "${CODEX_BIN}" || ! -x "${CODEX_BIN}" ]]; then
    echo "missing codex binary; set CODEX_BIN or add codex to PATH" >&2
    exit 1
fi
if [[ ! -d "${WORKDIR}" ]]; then
    echo "missing workdir: ${WORKDIR}" >&2
    exit 1
fi
if [[ ! -f "${PROMPT_FILE}" ]]; then
    echo "missing prompt file: ${PROMPT_FILE}" >&2
    exit 1
fi
require_positive_integer "${POLL_INTERVAL_SECONDS}" "--poll-seconds"
require_positive_integer "${MAX_RUNTIME_SECONDS}" "--max-runtime"
require_positive_integer "${STALE_AFTER_SECONDS}" "--stale-after"

log() {
    local message="$1"
    local stamp
    stamp="$(date '+%Y-%m-%d %H:%M:%S')"
    printf '[%s] %s\n' "${stamp}" "${message}" | tee -a "${LOG_FILE}" >&2
}

find_interactive_codex_pids() {
    ps -axo pid=,command= | awk -v bin="${CODEX_BIN}" '
        {
            pid = $1
            $1 = ""
            cmd = substr($0, 2)
            split(cmd, parts, /[[:space:]]+/)
            if (cmd ~ /app-server/ || cmd ~ / mcp-server / || cmd ~ / debug /) {
                next
            }
            if ((parts[1] == "node" && parts[2] == bin) ||
                parts[1] == bin) {
                print pid
            }
        }
    '
}

count_interactive_codex_sessions() {
    local pids
    pids="$(find_interactive_codex_pids || true)"
    if [[ -z "${pids}" ]]; then
        printf '0\n'
        return 0
    fi
    printf '%s\n' "${pids}" | awk 'NF {count += 1} END {print count + 0}'
}

interactive_codex_pgids() {
    local pids
    pids="$(find_interactive_codex_pids || true)"
    if [[ -z "${pids}" ]]; then
        return 0
    fi
    printf '%s\n' "${pids}" | while IFS= read -r pid; do
        [[ -z "${pid}" ]] && continue
        ps -o pgid= -p "${pid}" 2>/dev/null | tr -d '[:space:]'
        printf '\n'
    done | awk 'NF && !seen[$0]++ { print $0 }'
}

latest_codex_activity_epoch() {
    local files=(
        "${HOME}/.codex/log/codex-tui.log"
        "${HOME}/.codex/history.jsonl"
        "${HOME}/.codex/session_index.jsonl"
    )
    local latest=0
    local file=""
    local stamp=""

    for file in "${files[@]}"; do
        [[ ! -f "${file}" ]] && continue
        if stat -f '%m' "${file}" >/dev/null 2>&1; then
            stamp="$(stat -f '%m' "${file}")"
        else
            stamp="$(stat -c '%Y' "${file}" 2>/dev/null || true)"
        fi
        if [[ "${stamp}" =~ ^[0-9]+$ ]] && (( stamp > latest )); then
            latest="${stamp}"
        fi
    done

    printf '%s\n' "${latest}"
}

interactive_codex_is_stale() {
    local count
    count="$(count_interactive_codex_sessions)"
    if [[ "${count}" == "0" ]]; then
        return 1
    fi

    local latest
    latest="$(latest_codex_activity_epoch)"
    if [[ ! "${latest}" =~ ^[0-9]+$ ]] || [[ "${latest}" == "0" ]]; then
        return 0
    fi

    local now
    now="$(date +%s)"
    (( now - latest >= STALE_AFTER_SECONDS ))
}

kill_stale_codex_sessions() {
    local pgid=""
    local any=0
    while IFS= read -r pgid; do
        [[ -z "${pgid}" ]] && continue
        any=1
        if [[ "${DRY_RUN}" == "1" ]]; then
            log "dry-run: would terminate stale Codex process group ${pgid}"
            continue
        fi
        kill -TERM -- "-${pgid}" 2>/dev/null || true
    done < <(interactive_codex_pgids)

    if [[ "${DRY_RUN}" != "1" && "${any}" == "1" ]]; then
        sleep 1
        while IFS= read -r pgid; do
            [[ -z "${pgid}" ]] && continue
            kill -KILL -- "-${pgid}" 2>/dev/null || true
        done < <(interactive_codex_pgids)
    fi
}

launch_codex_session() {
    local launcher
    launcher="$(mktemp "${TMPDIR:-/tmp}/codex-watchdog-launch.XXXXXX")"
    mv "${launcher}" "${launcher}.sh"
    launcher="${launcher}.sh"
    cat >"${launcher}" <<EOF
#!/usr/bin/env bash
set -euo pipefail
trap 'rm -f "\$0"' EXIT
cd "$(printf '%s' "${WORKDIR}")"
PROMPT_CONTENT="\$(cat "$(printf '%s' "${PROMPT_FILE}")")"
if [[ "${RESUME_FIRST}" == "1" ]]; then
    if "$(printf '%s' "${CODEX_BIN}")" resume --last -C "$(printf '%s' "${WORKDIR}")" "\${PROMPT_CONTENT}"; then
        exit 0
    fi
fi
"$(printf '%s' "${CODEX_BIN}")" -C "$(printf '%s' "${WORKDIR}")" "\${PROMPT_CONTENT}"
EOF
    chmod +x "${launcher}"

    if [[ "${DRY_RUN}" == "1" ]]; then
        log "dry-run: would launch Codex using ${TERMINAL_APP} with ${launcher}"
        return 0
    fi

    if [[ "${TERMINAL_APP}" == "Terminal" ]]; then
        osascript - "${launcher}" <<'APPLESCRIPT'
on run argv
    set launchScript to item 1 of argv
    tell application "Terminal"
        activate
        do script ("bash " & quoted form of launchScript)
    end tell
end run
APPLESCRIPT
    elif [[ "${TERMINAL_APP}" == "iTerm" || "${TERMINAL_APP}" == "iTerm2" ]]; then
        osascript - "${launcher}" <<'APPLESCRIPT'
on run argv
    set launchScript to item 1 of argv
    tell application "iTerm"
        activate
        if (count of windows) is 0 then
            create window with default profile
        end if
        tell current window
            create tab with default profile command ("bash " & quoted form of launchScript)
        end tell
    end tell
end run
APPLESCRIPT
    else
        echo "unsupported terminal app: ${TERMINAL_APP}" >&2
        rm -f "${launcher}"
        return 1
    fi

    log "launched replacement Codex session in ${TERMINAL_APP}"
}

main() {
    local started_at
    started_at="$(date +%s)"
    log "watchdog started; monitoring interactive codex sessions for up to ${MAX_RUNTIME_SECONDS}s"

    while true; do
        local count
        count="$(count_interactive_codex_sessions)"
        if [[ "${count}" == "0" ]]; then
            log "no interactive codex session detected; relaunching"
            launch_codex_session
        elif interactive_codex_is_stale; then
            log "detected ${count} interactive codex session(s), but activity is stale"
            if [[ "${RESTART_STALE}" == "1" ]]; then
                kill_stale_codex_sessions
                launch_codex_session
            fi
        else
            log "detected ${count} interactive codex session(s); watchdog idle"
        fi

        if [[ "${ONCE}" == "1" ]]; then
            break
        fi
        if (( $(date +%s) - started_at >= MAX_RUNTIME_SECONDS )); then
            log "max runtime reached; exiting watchdog"
            break
        fi
        sleep "${POLL_INTERVAL_SECONDS}"
    done
}

main
