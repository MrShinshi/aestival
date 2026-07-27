/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include <cstdint>

namespace client {

// ─── system_resource_snapshot ─────────────────────────────────────────────
// Cross-platform process-level resource usage snapshot.
//
// Call collect_system_resources() from any thread.  The implementation
// caches the previous call's CPU counters internally (per-process static)
// so cpu_percent_recent reflects the delta since the last sampling.

struct system_resource_snapshot {
	double cpu_percent = 0.0;        // process CPU usage 0–100 (lifetime average)
	double cpu_percent_recent = 0.0; // recent CPU usage since last call (0 on first call)
	int64_t memory_rss_bytes = 0;    // resident set size (physical RAM)
	int64_t memory_virtual_bytes = 0; // virtual memory size
	int64_t system_memory_total_bytes = 0; // total physical RAM on the host
	int thread_count = 0;            // number of threads in this process
	int64_t uptime_seconds = 0;      // process uptime (seconds since start)
};

system_resource_snapshot collect_system_resources();

} // namespace client
