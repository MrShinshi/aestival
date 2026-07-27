/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "system_monitor.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#endif

#include <chrono>
#include <mutex>

namespace client {

namespace {

	// ── uptime tracking ──────────────────────────────────────────────────
	// Record process start time once; uptime = now - start.

	std::chrono::steady_clock::time_point g_process_start = std::chrono::steady_clock::now();

	// ── CPU delta state ──────────────────────────────────────────────────
	// The first call cannot compute a delta, so cpu_percent_recent returns 0.
	// Subsequent calls compare current counters against the cached previous values.

	std::mutex g_cpu_mutex;

#ifdef _WIN32

	struct cpu_cache {
		bool valid = false;       // false = no previous sample yet
		ULARGE_INTEGER proc_kernel{};
		ULARGE_INTEGER proc_user{};
		ULARGE_INTEGER sys_kernel{};
		ULARGE_INTEGER sys_user{};
		ULARGE_INTEGER sys_idle{};
	};
	cpu_cache g_cpu;

	double compute_cpu_percent(cpu_cache const& prev, cpu_cache const& cur, int cpu_count) {
		auto prev_proc = prev.proc_kernel.QuadPart + prev.proc_user.QuadPart;
		auto cur_proc  = cur.proc_kernel.QuadPart  + cur.proc_user.QuadPart;
		auto prev_sys  = prev.sys_kernel.QuadPart  + prev.sys_user.QuadPart  + prev.sys_idle.QuadPart;
		auto cur_sys   = cur.sys_kernel.QuadPart   + cur.sys_user.QuadPart   + cur.sys_idle.QuadPart;

		auto proc_delta = cur_proc - prev_proc;
		auto sys_delta  = cur_sys  - prev_sys;
		if (sys_delta == 0)
			return 0.0;
		return (static_cast<double>(proc_delta) / static_cast<double>(sys_delta)) * 100.0 * cpu_count;
	}

#else // Linux

	struct cpu_cache {
		bool valid = false;
		unsigned long long proc_ticks = 0;  // utime + stime + cutime + cstime
		unsigned long long sys_ticks = 0;   // sum of all CPU fields in /proc/stat
	};
	cpu_cache g_cpu;

	long g_clock_ticks = 0;  // sysconf(_SC_CLK_TCK), cached

	double compute_cpu_percent(cpu_cache const& prev, cpu_cache const& cur) {
		auto proc_delta = cur.proc_ticks - prev.proc_ticks;
		auto sys_delta  = cur.sys_ticks  - prev.sys_ticks;
		if (sys_delta == 0 || g_clock_ticks <= 0)
			return 0.0;
		return (static_cast<double>(proc_delta) / static_cast<double>(sys_delta)) * 100.0;
	}

#endif

} // namespace

// ─── collect_system_resources ───────────────────────────────────────────────

system_resource_snapshot collect_system_resources() {
	system_resource_snapshot snap;

	// Uptime
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
					   std::chrono::steady_clock::now() - g_process_start)
					   .count();
	snap.uptime_seconds = elapsed;

#ifdef _WIN32
	// ── Windows implementation ──────────────────────────────────────────

	// CPU times
	{
		FILETIME ft_create, ft_exit, ft_kernel, ft_user;
		if (GetProcessTimes(GetCurrentProcess(), &ft_create, &ft_exit, &ft_kernel, &ft_user)) {
			cpu_cache cur;
			cur.valid = true;
			cur.proc_kernel.LowPart  = ft_kernel.dwLowDateTime;
			cur.proc_kernel.HighPart = ft_kernel.dwHighDateTime;
			cur.proc_user.LowPart    = ft_user.dwLowDateTime;
			cur.proc_user.HighPart   = ft_user.dwHighDateTime;

			// System-wide idle/kernel/user times
			FILETIME ft_idle, ft_sys_kernel, ft_sys_user;
			if (GetSystemTimes(&ft_idle, &ft_sys_kernel, &ft_sys_user)) {
				cur.sys_idle.LowPart   = ft_idle.dwLowDateTime;
				cur.sys_idle.HighPart  = ft_idle.dwHighDateTime;
				cur.sys_kernel.LowPart  = ft_sys_kernel.dwLowDateTime;
				cur.sys_kernel.HighPart = ft_sys_kernel.dwHighDateTime;
				cur.sys_user.LowPart    = ft_sys_user.dwLowDateTime;
				cur.sys_user.HighPart   = ft_sys_user.dwHighDateTime;
			}

			// Lifetime average CPU (all time, not just recent)
			// Use total system time as denominator; approximate with available counters.
			{
				std::lock_guard<std::mutex> lk(g_cpu_mutex);
				if (g_cpu.valid) {
					SYSTEM_INFO si;
					GetSystemInfo(&si);
					snap.cpu_percent_recent = compute_cpu_percent(g_cpu, cur, si.dwNumberOfProcessors);
				}
				// Lifetime avg: proc time / wall-clock uptime (rough)
				auto total_proc_100ns = cur.proc_kernel.QuadPart + cur.proc_user.QuadPart;
				if (elapsed > 0)
					snap.cpu_percent = static_cast<double>(total_proc_100ns) / (elapsed * 10'000'000.0) * 100.0;
				g_cpu = cur;
			}
		}
	}

	// Memory
	{
		PROCESS_MEMORY_COUNTERS_EX pmc;
		pmc.cb = sizeof(pmc);
		if (K32GetProcessMemoryInfo(GetCurrentProcess(),
									reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
									sizeof(pmc))) {
			snap.memory_rss_bytes     = static_cast<int64_t>(pmc.WorkingSetSize);
			snap.memory_virtual_bytes = static_cast<int64_t>(pmc.PrivateUsage);
		}
	}

	// Thread count
	{
		HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (h != INVALID_HANDLE_VALUE) {
			THREADENTRY32 te;
			te.dwSize = sizeof(te);
			DWORD pid = GetCurrentProcessId();
			int count = 0;
			if (Thread32First(h, &te)) {
				do {
					if (te.th32OwnerProcessID == pid)
						++count;
				} while (Thread32Next(h, &te));
			}
			snap.thread_count = count;
			CloseHandle(h);
		}
	}

#else
	// ── Linux implementation ─────────────────────────────────────────────

	// Cache clock ticks once
	if (g_clock_ticks <= 0)
		g_clock_ticks = sysconf(_SC_CLK_TCK);

	// CPU and memory: /proc/self/stat
	// Fields of interest (1-indexed):
	//   14 utime, 15 stime, 16 cutime, 17 cstime,
	//   20 num_threads, 23 vsize (bytes), 24 rss (pages)
	{
		FILE* f = fopen("/proc/self/stat", "r");
		if (f) {
			// Read the comm field (field 2) which may contain spaces
			char comm_buf[256] = {};
			unsigned long long utime = 0, stime = 0, cutime = 0, cstime = 0;
			long num_threads = 0;
			unsigned long long vsize = 0;
			long rss_pages = 0;

			// Skip pid (field 1) and comm (field 2, in parens)
			int matched = fscanf(f,
				"%*d %255s"           // pid, comm
				" %*c"                // state
				" %*d %*d %*d %*d %*d" // ppid, pgrp, session, tty_nr, tpgid
				" %*u"                // flags
				" %*u %*u %*u %*u"    // minflt, cminflt, majflt, cmajflt
				" %llu %llu %llu %llu" // utime, stime, cutime, cstime
				" %*d"                // priority
				" %ld"                // num_threads
				" %*u"                // itrealvalue
				" %*llu"              // starttime
				" %llu"               // vsize
				" %ld",               // rss
				comm_buf,
				&utime, &stime, &cutime, &cstime,
				&num_threads,
				&vsize,
				&rss_pages);
			fclose(f);

			if (matched >= 7) {
				snap.thread_count = num_threads;
				snap.memory_virtual_bytes = static_cast<int64_t>(vsize);
				snap.memory_rss_bytes = static_cast<int64_t>(rss_pages) * sysconf(_SC_PAGESIZE);

				unsigned long long proc_ticks = utime + stime + cutime + cstime;

				// Read /proc/stat for total system CPU
				unsigned long long sys_total = 0;
				FILE* fs = fopen("/proc/stat", "r");
				if (fs) {
					char line[512];
					if (fgets(line, sizeof(line), fs)) {
						unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
						int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
									   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
						if (n >= 4)
							sys_total = user + nice + system + idle + iowait + irq + softirq + ((n >= 8) ? steal : 0);
					}
					fclose(fs);
				}

				{
					std::lock_guard<std::mutex> lk(g_cpu_mutex);
					if (g_cpu.valid && g_clock_ticks > 0) {
						cpu_cache cur;
						cur.valid = true;
						cur.proc_ticks = proc_ticks;
						cur.sys_ticks = sys_total;
						snap.cpu_percent_recent = compute_cpu_percent(g_cpu, cur);
					}
					// Lifetime average
					if (g_clock_ticks > 0 && elapsed > 0)
						snap.cpu_percent = static_cast<double>(proc_ticks) / g_clock_ticks / elapsed * 100.0;
					g_cpu.proc_ticks = proc_ticks;
					g_cpu.sys_ticks = sys_total;
					g_cpu.valid = true;
				}
			}
		}
	}

#endif

	return snap;
}

} // namespace client
