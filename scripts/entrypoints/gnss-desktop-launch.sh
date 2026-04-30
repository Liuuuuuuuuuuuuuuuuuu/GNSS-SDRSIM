#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")/../.." && pwd)"
cd "$ROOT_DIR"

LOG_DIR="$ROOT_DIR/runtime_logs"
mkdir -p "$LOG_DIR"
BUILD_LOG="$LOG_DIR/gnss-desktop-build.log"
PLAN_LOG="$LOG_DIR/gnss-desktop-plan.log"
BUILD_STATE_FILE="$LOG_DIR/.launcher-build-fingerprint"
GNSS_SPLASH_BIN="$ROOT_DIR/bin/gnss-loading-splash"
LOCK_DIR="/tmp/gnss-desktop-launch.lock"
MAP_READY_FILE="/tmp/gnss-map-ready-${USER:-user}-$$.flag"
CANCELLED_BY_USER=0
GUI_PRELAUNCHED=0
ACTIONABLE_RE='^[[:space:]]*(gcc|g\+\+|[^[:space:]]*nvcc|cc|c\+\+|ld|ar|sed |ln |rm |mkdir -p|bash |chmod )'
NPROC_VAL="$(nproc 2>/dev/null || echo 4)"
if ! [[ "$NPROC_VAL" =~ ^[0-9]+$ ]] || [ "$NPROC_VAL" -lt 1 ]; then
	NPROC_VAL="4"
fi
# Empirically, using ~75% of logical CPUs gives better full clean-build latency on this host.
DEFAULT_JOBS=$(( (NPROC_VAL * 3 + 3) / 4 ))
if [ "$DEFAULT_JOBS" -lt 1 ]; then
	DEFAULT_JOBS=1
fi
MAKE_JOBS="${GNSS_MAKE_JOBS:-$DEFAULT_JOBS}"
if ! [[ "$MAKE_JOBS" =~ ^[0-9]+$ ]] || [ "$MAKE_JOBS" -lt 1 ]; then
	MAKE_JOBS="$DEFAULT_JOBS"
fi

FORCE_CLEAN="${GNSS_FORCE_CLEAN:-0}"
if ! [[ "$FORCE_CLEAN" =~ ^[01]$ ]]; then
	FORCE_CLEAN=0
fi

BUILD_MODE="${GNSS_BUILD_MODE:-auto}"
if ! [[ "$BUILD_MODE" =~ ^(auto|clean|incremental)$ ]]; then
	BUILD_MODE="auto"
fi

BUILD_NEEDS_CLEAN=0
BUILD_DECISION_REASON=""
MAP_READY_MIN_TILES="${GNSS_MAP_READY_MIN_TILES:-18}"
if ! [[ "$MAP_READY_MIN_TILES" =~ ^[0-9]+$ ]] || [ "$MAP_READY_MIN_TILES" -lt 4 ]; then
	MAP_READY_MIN_TILES=18
fi
MAP_READY_WAIT_MS="${GNSS_MAP_READY_WAIT_MS:-12000}"
if ! [[ "$MAP_READY_WAIT_MS" =~ ^[0-9]+$ ]] || [ "$MAP_READY_WAIT_MS" -lt 1000 ]; then
	MAP_READY_WAIT_MS=12000
fi

CCACHE_BIN="$(command -v ccache 2>/dev/null || true)"
if [ -n "$CCACHE_BIN" ]; then
	export CCACHE="$CCACHE_BIN"
	export CCACHE_BASEDIR="$ROOT_DIR"
	export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-20G}"
	# Prioritize local speed over cache file compression latency.
	export CCACHE_COMPRESS="${CCACHE_COMPRESS:-0}"
fi

hash_file() {
	local path="$1"
	if [ ! -f "$path" ]; then
		printf 'missing\n'
		return 0
	fi
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$path" | awk '{print $1}'
		return 0
	fi
	cksum "$path" | awk '{print $1}'
}

compute_build_fingerprint() {
	local gcc_v gpp_v nvcc_v qt_v
	gcc_v="$(gcc --version 2>/dev/null | head -n 1 || true)"
	gpp_v="$(g++ --version 2>/dev/null | head -n 1 || true)"
	nvcc_v="$(nvcc --version 2>/dev/null | tail -n 1 || true)"
	qt_v="$(pkg-config --modversion Qt6Core 2>/dev/null || pkg-config --modversion Qt5Core 2>/dev/null || true)"
	printf '%s\n' \
		"gcc=$gcc_v" \
		"g++=$gpp_v" \
		"nvcc=$nvcc_v" \
		"qt=$qt_v" \
		"jobs=$MAKE_JOBS" \
		"ccache=${CCACHE_BIN:-none}" \
		"makefile=$(hash_file \"$ROOT_DIR/Makefile\")" \
		"cuda_targets=$(hash_file \"$ROOT_DIR/cuda/cuda_targets.mk\")"
}

decide_build_mode() {
	if [ "$FORCE_CLEAN" -eq 1 ]; then
		BUILD_NEEDS_CLEAN=1
		BUILD_DECISION_REASON="GNSS_FORCE_CLEAN=1"
		return 0
	fi

	case "$BUILD_MODE" in
		clean)
			BUILD_NEEDS_CLEAN=1
			BUILD_DECISION_REASON="GNSS_BUILD_MODE=clean"
			return 0
			;;
		incremental)
			BUILD_NEEDS_CLEAN=0
			BUILD_DECISION_REASON="GNSS_BUILD_MODE=incremental"
			return 0
			;;
		auto)
			local current_fp previous_fp
			current_fp="$(compute_build_fingerprint)"
			if [ ! -f "$BUILD_STATE_FILE" ]; then
				BUILD_NEEDS_CLEAN=1
				BUILD_DECISION_REASON="auto: first run"
				printf '%s\n' "$current_fp" > "$BUILD_STATE_FILE.tmp"
				mv -f "$BUILD_STATE_FILE.tmp" "$BUILD_STATE_FILE"
				return 0
			fi
			previous_fp="$(cat "$BUILD_STATE_FILE" 2>/dev/null || true)"
			if [ "$current_fp" != "$previous_fp" ]; then
				BUILD_NEEDS_CLEAN=1
				BUILD_DECISION_REASON="auto: toolchain/config changed"
			else
				BUILD_NEEDS_CLEAN=0
				BUILD_DECISION_REASON="auto: no toolchain/config change"
			fi
			printf '%s\n' "$current_fp" > "$BUILD_STATE_FILE.tmp"
			mv -f "$BUILD_STATE_FILE.tmp" "$BUILD_STATE_FILE"
			return 0
			;;
	esac
}

cleanup_lock() {
	rm -rf "$LOCK_DIR"
	rm -f "$MAP_READY_FILE"
}

if ! mkdir "$LOCK_DIR" 2>/dev/null; then
	exit 0
fi
trap cleanup_lock EXIT

show_build_error() {
	local tail_log
	tail_log="$(tail -n 25 "$BUILD_LOG" 2>/dev/null || true)"
	if command -v zenity >/dev/null 2>&1; then
		zenity --error \
			--title="GNSS Build Failed" \
			--width=760 \
			--text="Build failed.\n\nLast output:\n$tail_log" >/dev/null 2>&1 || true
	elif command -v notify-send >/dev/null 2>&1; then
		notify-send "GNSS" "Build failed. Check: $BUILD_LOG"
	fi
}

launch_simulator() {
	rm -f "$MAP_READY_FILE"
	GNSS_MAP_READY_FILE="$MAP_READY_FILE" \
	GNSS_MAP_READY_MIN_TILES="$MAP_READY_MIN_TILES" \
	nohup "$ROOT_DIR/gnss-sim" >/dev/null 2>&1 &
	GNSS_PID="$!"
}

wait_for_map_ready() {
	local timeout_ms="${1:-12000}"
	local waited=0
	while [ "$waited" -lt "$timeout_ms" ]; do
		if [ -f "$MAP_READY_FILE" ]; then
			return 0
		fi
		if [ -n "${GNSS_PID:-}" ] && ! kill -0 "$GNSS_PID" 2>/dev/null; then
			return 1
		fi
		sleep 0.10
		waited=$((waited + 100))
	done
	return 1
}

is_gui_ready() {
	# Prefer PID-bound detection: only accept windows owned by this launch.
	if [ -z "${GNSS_PID:-}" ]; then
		return 1
	fi

	if command -v xprop >/dev/null 2>&1; then
		local wins
		wins="$(xprop -root _NET_CLIENT_LIST 2>/dev/null | sed -n 's/.*# //p' | tr ',' ' ')"
		for wid in $wins; do
			wid="${wid// /}"
			[ -z "$wid" ] && continue
			local wp
			wp="$(xprop -id "$wid" _NET_WM_PID 2>/dev/null | awk -F' = ' '/_NET_WM_PID/{print $2}')"
			if [ -n "$wp" ] && [ "$wp" = "$GNSS_PID" ]; then
				# Window exists for this process and has WM_STATE -> mapped.
				xprop -id "$wid" WM_STATE >/dev/null 2>&1 && return 0
			fi
		done
	fi

	# Fallback for environments without _NET_WM_PID support.
	if command -v xwininfo >/dev/null 2>&1; then
		xwininfo -root -tree 2>/dev/null | grep -Eiq '"GNSS"|\bgnss\b|gnss-sim' && return 0
	fi
	return 1
}

wait_for_gui_ready() {
	local timeout_ms="${1:-10000}"
	local waited=0
	while [ "$waited" -lt "$timeout_ms" ]; do
		if is_gui_ready; then
			return 0
		fi
		# If process already exited, stop waiting.
		if [ -n "${GNSS_PID:-}" ] && ! kill -0 "$GNSS_PID" 2>/dev/null; then
			return 1
		fi
		sleep 0.05
		waited=$((waited + 50))
	done
	return 1
}

count_actionable_steps() {
	local file="$1"
	if [ ! -s "$file" ]; then
		printf '0\n'
		return 0
	fi
	(grep -E "$ACTIONABLE_RE" "$file" || true) | wc -l | tr -d ' '
}

last_progress_message() {
	if [ -s "$BUILD_LOG" ] && tail -n 80 "$BUILD_LOG" | grep -q "error:"; then
		printf 'Build issue detected, checking...\n'
		return
	fi
	printf 'Compiling GNSS modules...\n'
}

start_progress_dialog() {
	if [ -x "$GNSS_SPLASH_BIN" ] && [ -n "${DISPLAY:-}" ]; then
		coproc SPLASH_PROC {
			"$GNSS_SPLASH_BIN" "$ROOT_DIR/assets/gnss-logo.png"
		}
		SPLASH_PID="$SPLASH_PROC_PID"
		PROGRESS_FD="${SPLASH_PROC[1]}"
		return 0
	fi

	# Fallback keeps UI usable even when splash binary is unavailable.
	if [ -n "${DISPLAY:-}" ] && command -v zenity >/dev/null 2>&1; then
		coproc SPLASH_PROC {
			zenity --progress \
				--title="GNSS" \
				--text="Preparing GNSS build..." \
				--percentage=0 \
				--no-cancel \
				--auto-close \
				--auto-kill \
				--width=520
		}
		SPLASH_PID="$SPLASH_PROC_PID"
		PROGRESS_FD="${SPLASH_PROC[1]}"
	fi
}


progress_update() {
	if [ -n "${SPLASH_PID:-}" ] && kill -0 "$SPLASH_PID" 2>/dev/null && [ -n "${PROGRESS_FD:-}" ]; then
		printf 'PROGRESS\t%s\t%s\n' "$1" "$2" >&"$PROGRESS_FD"
	fi
}

progress_error() {
	if [ -n "${SPLASH_PID:-}" ] && kill -0 "$SPLASH_PID" 2>/dev/null && [ -n "${PROGRESS_FD:-}" ]; then
		printf 'ERROR\t%s\n' "$1" >&"$PROGRESS_FD"
	fi
}

finish_progress_dialog() {
	local close_delay_ms="${1:-100}"
	if [ -n "${SPLASH_PID:-}" ] && kill -0 "$SPLASH_PID" 2>/dev/null && [ -n "${PROGRESS_FD:-}" ]; then
		printf 'CLOSE\t%s\n' "$close_delay_ms" >&"$PROGRESS_FD" || true
	fi
	if [ -n "${PROGRESS_FD:-}" ]; then
		exec {PROGRESS_FD}>&-
	fi
	if [ -n "${SPLASH_PID:-}" ]; then
		wait "$SPLASH_PID" 2>/dev/null || true
	fi
}

track_build_progress() {
	local pid="$1"
	local total_steps="$2"
	local done_steps=0
	local last_pct=6
	local last_line=0
	progress_update "2" "Preparing GNSS build plan..."
	while kill -0 "$pid" 2>/dev/null; do
		if [ -n "${SPLASH_PID:-}" ] && ! kill -0 "$SPLASH_PID" 2>/dev/null; then
			CANCELLED_BY_USER=1
			kill "$pid" 2>/dev/null || true
			wait "$pid" 2>/dev/null || true
			return 130
		fi

		local pct=8
		local msg
		msg="$(last_progress_message)"
		local current_line new_steps
		current_line="$(wc -l < "$BUILD_LOG" | tr -d ' ')"
		if [ "$current_line" -gt "$last_line" ]; then
			new_steps="$(sed -n "$((last_line + 1)),$((current_line))p" "$BUILD_LOG" | grep -E "$ACTIONABLE_RE" | wc -l | tr -d ' ')"
			done_steps=$((done_steps + new_steps))
			last_line="$current_line"
		fi
		if [ "$total_steps" -gt 0 ]; then
			if [ "$done_steps" -gt "$total_steps" ]; then
				done_steps="$total_steps"
			fi
			pct=$((6 + (done_steps * 88 / total_steps)))
			if [ "$pct" -gt 92 ]; then
				pct=92
			fi
			if [ "$pct" -lt "$last_pct" ]; then
				pct="$last_pct"
			fi
			last_pct="$pct"
			msg="Compiling GNSS modules... (${done_steps}/${total_steps})"
		else
			pct="$last_pct"
			msg="Build check in progress..."
		fi
		progress_update "$pct" "$msg"
		sleep 0.12
	done
}

track_launch_progress() {
	if [ -n "${SPLASH_PID:-}" ] && ! kill -0 "$SPLASH_PID" 2>/dev/null; then
		CANCELLED_BY_USER=1
		return 130
	fi
	progress_update "95" "Build complete. Preparing GNSS GUI..."
	if [ -n "${SPLASH_PID:-}" ] && ! kill -0 "$SPLASH_PID" 2>/dev/null; then
		CANCELLED_BY_USER=1
		return 130
	fi
	launch_simulator
	GUI_PRELAUNCHED=1
	progress_update "96" "Opening GNSS GUI..."
	if wait_for_gui_ready 10000; then
		progress_update "97" "Rendering map tiles..."
		if wait_for_map_ready "$MAP_READY_WAIT_MS"; then
			progress_update "100" "Map ready. Entering GNSS GUI..."
		else
			progress_update "100" "Opening GNSS GUI..."
		fi
	else
		# Fail-safe: do not get stuck forever.
		progress_update "100" "Opening GNSS GUI..."
	fi
}

: >"$BUILD_LOG"
start_progress_dialog
decide_build_mode
if [ "$BUILD_NEEDS_CLEAN" -eq 1 ]; then
	progress_update "1" "Cleaning previous build..."
	printf '[%s] make clean\n' "$(date '+%F %T')" >>"$BUILD_LOG"
	if ! make clean >>"$BUILD_LOG" 2>&1; then
		progress_error "Build failed while cleaning"
		finish_progress_dialog
		show_build_error
		exit 1
	fi
else
	progress_update "1" "Incremental build (fast mode)..."
	printf '[%s] skip make clean\n' "$(date '+%F %T')" >>"$BUILD_LOG"
fi

mkdir -p "$LOG_DIR"

make -n -j"$MAKE_JOBS" >"$PLAN_LOG" 2>/dev/null || true
printf '[%s] desktop launch build\n' "$(date '+%F %T')" >>"$BUILD_LOG"
printf '[%s] make jobs: %s (nproc=%s)\n' "$(date '+%F %T')" "$MAKE_JOBS" "$NPROC_VAL" >>"$BUILD_LOG"
printf '[%s] force clean: %s\n' "$(date '+%F %T')" "$FORCE_CLEAN" >>"$BUILD_LOG"
printf '[%s] build mode: %s\n' "$(date '+%F %T')" "$BUILD_MODE" >>"$BUILD_LOG"
printf '[%s] auto clean decision: %s (%s)\n' "$(date '+%F %T')" "$BUILD_NEEDS_CLEAN" "$BUILD_DECISION_REASON" >>"$BUILD_LOG"
printf '[%s] map-ready gate: tiles>=%s wait_ms=%s file=%s\n' "$(date '+%F %T')" "$MAP_READY_MIN_TILES" "$MAP_READY_WAIT_MS" "$MAP_READY_FILE" >>"$BUILD_LOG"
if [ -n "$CCACHE_BIN" ]; then
	printf '[%s] ccache: enabled (%s)\n' "$(date '+%F %T')" "$CCACHE_BIN" >>"$BUILD_LOG"
else
	printf '[%s] ccache: not found\n' "$(date '+%F %T')" >>"$BUILD_LOG"
fi

make -j"$MAKE_JOBS" >>"$BUILD_LOG" 2>&1 &
BUILD_PID="$!"
TOTAL_STEPS="$(count_actionable_steps "$PLAN_LOG")"
track_build_progress "$BUILD_PID" "$TOTAL_STEPS" || true

if [ "$CANCELLED_BY_USER" -eq 1 ]; then
	exit 0
fi

if ! wait "$BUILD_PID"; then
	progress_error "Build failed"
	finish_progress_dialog
	show_build_error
	exit 1
fi

track_launch_progress || true
if [ "$CANCELLED_BY_USER" -eq 1 ]; then
	exit 0
fi
if [ "$GUI_PRELAUNCHED" -eq 0 ]; then
	launch_simulator
fi
finish_progress_dialog 10
