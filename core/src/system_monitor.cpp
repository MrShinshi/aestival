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
#include <unistd.h>
#endif

#include <chrono>
#include <mutex>

namespace client {

namespace {

// ── uptime tracking ──────────────────────────────────────────────────
std::chrono::steady_clock::time_point g_process_start = std::chrono::steady_clock::now();

// ── CPU delta state ──────────────────────────────────────────────────
std::mutex g_cpu_mutex;

#ifdef _WIN32

struct cpu_cache {
	bool valid = false;
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
	unsigned long long proc_ticks = 0;
	unsigned long long sys_ticks = 0;
};
cpu_cache g_cpu;

long g_clock_ticks = 0;

double compute_cpu_percent(cpu_cache const& prev, cpu_cache const& cur) {
	auto proc_delta = cur.proc_ticks - prev.proc_ticks;
	auto sys_delta  = cur.sys_ticks  - prev.sys_ticks;
	if (sys_delta == 0 || g_clock_ticks <= 0)
		return 0.0;
	return (static_cast<double>(proc_delta) / static_cast<double>(sys_delta)) * 100.0;
}

#endif

} // namespace

// ─── collect_system_resources ─────────────────────────────────────────

system_resource_snapshot collect_system_resources() {
	system_resource_snapshot snap;

	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
					   std::chrono::steady_clock::now() - g_process_start)
					   .count();
	snap.uptime_seconds = elapsed;

#ifdef _WIN32
	// ── Windows implementation ──────────────────────────────────────

	// CPU
	{
		FILETIME ft_create, ft_exit, ft_kernel, ft_user;
		if (GetProcessTimes(GetCurrentProcess(), &ft_create, &ft_exit, &ft_kernel, &ft_user)) {
			cpu_cache cur;
			cur.valid = true;
			cur.proc_kernel.LowPart  = ft_kernel.dwLowDateTime;
			cur.proc_kernel.HighPart = ft_kernel.dwHighDateTime;
			cur.proc_user.LowPart    = ft_user.dwLowDateTime;
			cur.proc_user.HighPart   = ft_user.dwHighDateTime;

			FILETIME ft_idle, ft_sys_kernel, ft_sys_user;
			if (GetSystemTimes(&ft_idle, &ft_sys_kernel, &ft_sys_user)) {
				cur.sys_idle.LowPart   = ft_idle.dwLowDateTime;
				cur.sys_idle.HighPart  = ft_idle.dwHighDateTime;
				cur.sys_kernel.LowPart  = ft_sys_kernel.dwLowDateTime;
				cur.sys_kernel.HighPart = ft_sys_kernel.dwHighDateTime;
				cur.sys_user.LowPart    = ft_sys_user.dwLowDateTime;
				cur.sys_user.HighPart   = ft_sys_user.dwHighDateTime;
			}

			{
				std::lock_guard<std::mutex> lk(g_cpu_mutex);
				if (g_cpu.valid) {
					SYSTEM_INFO si;
					GetSystemInfo(&si);
					snap.cpu_percent_recent = compute_cpu_percent(g_cpu, cur, si.dwNumberOfProcessors);
				}
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
	// ── Linux implementation ─────────────────────────────────────────

	if (g_clock_ticks <= 0)
		g_clock_ticks = sysconf(_SC_CLK_TCK);

	// /proc/self/stat — format: "pid (comm) state ..."
	// After the ')' comes: state ppid pgrp session tty_nr tpgid flags
	//   minflt cminflt majflt cmajflt utime stime cutime cstime
	//   priority nice num_threads itrealvalue starttime vsize rss
	{
		FILE* f = fopen("/proc/self/stat", "r");
		if (f) {
			char line[1024] = {};
			if (fgets(line, sizeof(line), f)) {
				char const* after_comm = strrchr(line, ')');
				if (after_comm) {
					++after_comm;
					unsigned long long utime = 0, stime = 0, cutime = 0, cstime = 0;
					long num_threads = 0;
					unsigned long long vsize = 0;
					long rss_pages = 0;

					int matched = sscanf(after_comm,
						" %*c"                // state
						" %*d %*d %*d %*d %*d" // ppid..tpgid
						" %*u"                // flags
						" %*u %*u %*u %*u"    // minflt..cmajflt
						" %llu %llu %llu %llu" // utime, stime, cutime, cstime
						" %*d %*d"            // priority, nice
						" %ld"                // num_threads
						" %*u"                // itrealvalue
						" %*llu"              // starttime
						" %llu"               // vsize
						" %ld",               // rss
						&utime, &stime, &cutime, &cstime,
						&num_threads,
						&vsize,
						&rss_pages);

					if (matched >= 7) {
						snap.thread_count = num_threads;
						snap.memory_virtual_bytes = static_cast<int64_t>(vsize);
						snap.memory_rss_bytes = static_cast<int64_t>(rss_pages) * sysconf(_SC_PAGESIZE);

						unsigned long long proc_ticks = utime + stime + cutime + cstime;

						// /proc/stat for system-wide CPU
						unsigned long long sys_total = 0;
						FILE* fs = fopen("/proc/stat", "r");
						if (fs) {
							char stat_line[512];
							if (fgets(stat_line, sizeof(stat_line), fs)) {
								unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
								int n = sscanf(stat_line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
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
							if (g_clock_ticks > 0 && elapsed > 0)
								snap.cpu_percent = static_cast<double>(proc_ticks) / g_clock_ticks / elapsed * 100.0;
							g_cpu.proc_ticks = proc_ticks;
							g_cpu.sys_ticks = sys_total;
							g_cpu.valid = true;
						}
					}
				}
			}
			fclose(f);
		}
	}

#endif

	return snap;
}

} // namespace client
