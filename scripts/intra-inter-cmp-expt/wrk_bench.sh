#!/usr/bin/env bash
# Usage examples:
#   ./bench_sidecars.sh --node-ip 10.10.1.1
#   ./bench_sidecars.sh --node-ip 10.10.1.1 --max-conc 64 --duration 30s --max-threads 8 --out results.csv

set -euo pipefail

# Defaults (overridable via flags below)
MAX_CONC="${MAX_CONC:-64}"
DURATION="${DURATION:-30s}"
MAX_THREADS="${MAX_THREADS:-4}"
OUT="${OUT:-sidecar_bench_$(date +%Y%m%d_%H%M%S).csv}"
NODE_IP="${NODE_IP:-}"

# ----- CLI flags -----
while [[ $# -gt 0 ]]; do
  case "$1" in
    --node-ip)     NODE_IP="$2"; shift 2 ;;
    --max-conc)    MAX_CONC="$2"; shift 2 ;;
    --duration)    DURATION="$2"; shift 2 ;;
    --max-threads) MAX_THREADS="$2"; shift 2 ;;
    --out)         OUT="$2"; shift 2 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing required command: $1" >&2; exit 1; }; }
need wrk; need awk; need sed

# ----- Helpers -----
to_ms() {
  # Convert latency token to milliseconds (supports us/ms/s)
  local tok="${1:-}"; [[ -n "$tok" ]] || { echo "NA"; return; }
  local num unit
  num="$(printf '%s' "$tok" | sed -E 's/^([0-9.]+).*/\1/')"
  unit="$(printf '%s' "$tok" | sed -E 's/^[0-9.]+(.*)$/\1/')"
  case "$unit" in
    us) awk -v n="$num" 'BEGIN{printf "%.6f", n/1000.0}' ;;
    ms) awk -v n="$num" 'BEGIN{printf "%.3f", n}' ;;
    s)  awk -v n="$num" 'BEGIN{printf "%.3f", n*1000.0}' ;;
    *)  printf "%s" "$num" ;;
  esac
}

from_si() {
  # Convert Req/Sec tokens with optional SI suffix (k/M/G) to plain number
  local tok="${1:-}"; [[ -n "$tok" ]] || { echo "NA"; return; }
  if [[ "$tok" =~ ^[0-9.]+$ ]]; then printf "%s" "$tok"; return; fi
  local num="${tok%[a-zA-Z]}"; local suf="${tok:${#num}}"
  case "$suf" in
    k|K) awk -v n="$num" 'BEGIN{printf "%.6f", n*1000.0}' ;;
    M)   awk -v n="$num" 'BEGIN{printf "%.6f", n*1000000.0}' ;;
    G)   awk -v n="$num" 'BEGIN{printf "%.6f", n*1000000000.0}' ;;
    m)   awk -v n="$num" 'BEGIN{printf "%.6f", n/1000.0}' ;;
    *)   printf "%s" "$num" ;;
  esac
}

strip_pct() {
  local tok="${1:-}"; [[ -n "$tok" ]] || { echo "NA"; return; }
  printf '%s' "$tok" | sed 's/%$//'
}

# Map concurrency -> threads: +1 thread per 8 concurrency (1–8 => 1, 9–16 => 2, ...)
threads_for_conc() {
  local c="$1"
  local t=$(( 1 + ( (c - 1) / 8 ) ))
  # Cap by c and MAX_THREADS
  if (( t > c )); then t="$c"; fi
  if (( t > MAX_THREADS )); then t="$MAX_THREADS"; fi
  echo "$t"
}

# ----- Ports -----
BASE_PORT=8080

echo "Using NODE_IP=$NODE_IP"
echo " baseline:    http://$NODE_IP:$BASE_PORT/"
echo

# ----- CSV header -----
echo "variant,concurrency,threads,lat_avg_ms,lat_stdev_ms,lat_max_ms,lat_pm_stdev_pct,reqps_thread_avg,reqps_thread_stdev,reqps_thread_max,reqps_thread_pm_stdev_pct,requests_per_sec" > "$OUT"

run_one() {
  local name="$1" port="$2"
  for ((c=1; c<=MAX_CONC; c++)); do
    local t
    t="$(threads_for_conc "$c")"
    (( t == 0 )) && t=1
    echo "[$name] c=$c t=$t  ->  http://$NODE_IP:$port/1"
    out="$(wrk -t"$t" -c"$c" -d"$DURATION" "http://$NODE_IP:$port/1" 2>&1 || true)"

    # Parse Thread Stats lines
    # Expect: "Latency <avg> <stdev> <max> <+/-stdev%>"
    read -r lat_avg_tok lat_stdev_tok lat_max_tok lat_pm_tok < <(printf '%s\n' "$out" | awk '$1=="Latency"{print $2,$3,$4,$5; exit}')
    read -r rps_avg_tok rps_stdev_tok rps_max_tok rps_pm_tok  < <(printf '%s\n' "$out" | awk '$1=="Req/Sec"{print $2,$3,$4,$5; exit}')

    lat_avg_ms="$(to_ms "$lat_avg_tok")"
    lat_stdev_ms="$(to_ms "$lat_stdev_tok")"
    lat_max_ms="$(to_ms "$lat_max_tok")"
    lat_pm_pct="$(strip_pct "$lat_pm_tok")"

    rps_thread_avg="$(from_si "$rps_avg_tok")"
    rps_thread_stdev="$(from_si "$rps_stdev_tok")"
    rps_thread_max="$(from_si "$rps_max_tok")"
    rps_thread_pm_pct="$(strip_pct "$rps_pm_tok")"

    # Aggregated Requests/sec (all threads)
    rps_agg="$(printf '%s\n' "$out" | awk -F: '/Requests\/sec/ {gsub(/^[ \t]+/,"",$2); print $2; exit}')"
    rps_agg="${rps_agg:-NA}"

    echo "$name,$c,$t,$lat_avg_ms,$lat_stdev_ms,$lat_max_ms,$lat_pm_pct,$rps_thread_avg,$rps_thread_stdev,$rps_thread_max,$rps_thread_pm_pct,$rps_agg" >> "$OUT"

    sleep 2
  done
}

run_one baseline    "$BASE_PORT"

echo
echo "Results written to: $OUT"

# #!/usr/bin/env bash
# # Usage examples:
# #   ./bench_sidecars.sh --node-ip 10.10.1.1
# #   ./bench_sidecars.sh --node-ip 10.10.1.1 --max-conc 64 --duration 30s --max-threads 8 --out results.csv

# set -euo pipefail

# # Defaults (overridable via flags below)
# MAX_CONC="${MAX_CONC:-64}"
# DURATION="${DURATION:-30s}"
# MAX_THREADS="${MAX_THREADS:-4}"
# OUT="${OUT:-sidecar_bench_$(date +%Y%m%d_%H%M%S).csv}"
# NODE_IP="${NODE_IP:-}"

# # ----- CLI flags -----
# while [[ $# -gt 0 ]]; do
#   case "$1" in
#     --node-ip)     NODE_IP="$2"; shift 2 ;;
#     --max-conc)    MAX_CONC="$2"; shift 2 ;;
#     --duration)    DURATION="$2"; shift 2 ;;
#     --max-threads) MAX_THREADS="$2"; shift 2 ;;
#     --out)         OUT="$2"; shift 2 ;;
#     *) echo "Unknown arg: $1" >&2; exit 1 ;;
#   esac
# done

# need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing required command: $1" >&2; exit 1; }; }
# need wrk; need awk; need sed

# # ----- Helpers -----
# to_ms() {
#   # Convert latency token to milliseconds (supports us/ms/s)
#   local tok="${1:-}"; [[ -n "$tok" ]] || { echo "NA"; return; }
#   local num unit
#   num="$(printf '%s' "$tok" | sed -E 's/^([0-9.]+).*/\1/')"
#   unit="$(printf '%s' "$tok" | sed -E 's/^[0-9.]+(.*)$/\1/')"
#   case "$unit" in
#     us) awk -v n="$num" 'BEGIN{printf "%.6f", n/1000.0}' ;;
#     ms) awk -v n="$num" 'BEGIN{printf "%.3f", n}' ;;
#     s)  awk -v n="$num" 'BEGIN{printf "%.3f", n*1000.0}' ;;
#     *)  printf "%s" "$num" ;;
#   esac
# }

# from_si() {
#   # Convert Req/Sec tokens with optional SI suffix (k/M/G) to plain number
#   local tok="${1:-}"; [[ -n "$tok" ]] || { echo "NA"; return; }
#   if [[ "$tok" =~ ^[0-9.]+$ ]]; then printf "%s" "$tok"; return; fi
#   local num="${tok%[a-zA-Z]}"; local suf="${tok:${#num}}"
#   case "$suf" in
#     k|K) awk -v n="$num" 'BEGIN{printf "%.6f", n*1000.0}' ;;
#     M)   awk -v n="$num" 'BEGIN{printf "%.6f", n*1000000.0}' ;;
#     G)   awk -v n="$num" 'BEGIN{printf "%.6f", n*1000000000.0}' ;;
#     m)   awk -v n="$num" 'BEGIN{printf "%.6f", n/1000.0}' ;;  # unlikely, but safe
#     *)   printf "%s" "$num" ;;
#   esac
# }

# strip_pct() {
#   local tok="${1:-}"; [[ -n "$tok" ]] || { echo "NA"; return; }
#   printf '%s' "$tok" | sed 's/%$//'
# }

# # ----- Ports -----
# BASE_PORT=8080

# echo "Using NODE_IP=$NODE_IP"
# echo " baseline:    http://$NODE_IP:$BASE_PORT/"
# echo

# # ----- CSV header -----
# echo "variant,concurrency,threads,lat_avg_ms,lat_stdev_ms,lat_max_ms,lat_pm_stdev_pct,reqps_thread_avg,reqps_thread_stdev,reqps_thread_max,reqps_thread_pm_stdev_pct,requests_per_sec" > "$OUT"

# run_one() {
#   local name="$1" port="$2"
#   for ((c=1; c<=MAX_CONC; c++)); do
#     local t="$MAX_THREADS"; (( t > c )) && t="$c"; (( t == 0 )) && t=1
#     echo "[$name] c=$c t=$t  ->  http://$NODE_IP:$port/1"
#     out="$(wrk -t"$t" -c"$c" -d"$DURATION" "http://$NODE_IP:$port/1" 2>&1 || true)"

#     # Parse Thread Stats lines
#     # Expect: "Latency <avg> <stdev> <max> <+/-stdev%>"
#     read -r lat_avg_tok lat_stdev_tok lat_max_tok lat_pm_tok < <(printf '%s\n' "$out" | awk '$1=="Latency"{print $2,$3,$4,$5; exit}')
#     read -r rps_avg_tok rps_stdev_tok rps_max_tok rps_pm_tok  < <(printf '%s\n' "$out" | awk '$1=="Req/Sec"{print $2,$3,$4,$5; exit}')

#     lat_avg_ms="$(to_ms "$lat_avg_tok")"
#     lat_stdev_ms="$(to_ms "$lat_stdev_tok")"
#     lat_max_ms="$(to_ms "$lat_max_tok")"
#     lat_pm_pct="$(strip_pct "$lat_pm_tok")"

#     rps_thread_avg="$(from_si "$rps_avg_tok")"
#     rps_thread_stdev="$(from_si "$rps_stdev_tok")"
#     rps_thread_max="$(from_si "$rps_max_tok")"
#     rps_thread_pm_pct="$(strip_pct "$rps_pm_tok")"

#     # Aggregated Requests/sec (all threads)
#     rps_agg="$(printf '%s\n' "$out" | awk -F: '/Requests\/sec/ {gsub(/^[ \t]+/,"",$2); print $2; exit}')"
#     rps_agg="${rps_agg:-NA}"

#     echo "$name,$c,$t,$lat_avg_ms,$lat_stdev_ms,$lat_max_ms,$lat_pm_pct,$rps_thread_avg,$rps_thread_stdev,$rps_thread_max,$rps_thread_pm_pct,$rps_agg" >> "$OUT"

#     sleep 2
#   done
# }

# run_one baseline    "$BASE_PORT"

# echo
# echo "Results written to: $OUT"