/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "agent_reach_client.h"

#include "encode_utils.h"
#include "log.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

// ─── shell_escape ─────────────────────────────────────────────────────────
// Escape a string for insertion inside a double-quoted shell argument.
static std::string shell_escape(std::string_view s) {
	std::string r;
	r.reserve(s.size());
	for (char c : s) {
		switch (c) {
		case '"': r += "\\\""; break;
		case '\\': r += "\\\\"; break;
		case '$': r += "\\$"; break;
		case '`': r += "\\`"; break;
		case '!': r += "\\!"; break;
		case '|': r += "\\|"; break;
		case '&': r += "\\&"; break;
		case '^': r += "\\^"; break;
		case '\n':
		case '\r': r += ' '; break;
		default: r += c; break;
		}
	}
	return r;
}

} // namespace

// ─── exec ─────────────────────────────────────────────────────────────────
// Synchronous subprocess execution with timeout watchdog.

std::string client::agent_reach_client::exec(std::string_view cmd, std::chrono::seconds timeout) {
	std::string full_cmd(cmd);
	full_cmd += " 2>&1";

#ifdef _WIN32
	struct pipe_guard {
		FILE* f = nullptr;
		~pipe_guard() {
			if (f)
				_pclose(f);
		}
	};

	HANDLE hChildStdoutRd = nullptr, hChildStdoutWr = nullptr;
	SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
	if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &sa, 0))
		return {};

	SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0);

	PROCESS_INFORMATION pi = {};
	STARTUPINFOW si = {sizeof(STARTUPINFOW)};
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = hChildStdoutWr;
	si.hStdError = hChildStdoutWr;

	std::string path_prefix;
	path_prefix += "set \"PATH=";
	if (auto* home = std::getenv("USERPROFILE"))
		path_prefix += std::string(home) + "\\python\\Scripts;";
	if (auto* appdata = std::getenv("APPDATA"))
		path_prefix += std::string(appdata) + "\\npm;";
	if (auto* pf = std::getenv("ProgramFiles"))
		path_prefix += std::string(pf) + "\\GitHub CLI;";
	if (auto* pf86 = std::getenv("ProgramFiles(x86)"))
		path_prefix += std::string(pf86) + "\\GitHub CLI;";
	path_prefix += "%PATH%\" && ";

	static auto escape_cmd_meta = [](std::string const& s) -> std::string {
		std::string r;
		r.reserve(s.size() * 2);
		for (char ch : s) {
			switch (ch) {
			case '&':
			case '|':
			case '<':
			case '>':
			case '^': r += '^'; break;
			case '%': r += '%'; break;
			}
			r += ch;
		}
		return r;
	};

	std::wstring wide_cmd(path_prefix.begin(), path_prefix.end());
	std::string escaped = escape_cmd_meta(full_cmd);
	wide_cmd.append(escaped.begin(), escaped.end());
	std::wstring cmd_line = L"cmd.exe /c " + wide_cmd;

	if (!CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
						&pi)) {
		CloseHandle(hChildStdoutWr);
		CloseHandle(hChildStdoutRd);
		return {};
	}
	CloseHandle(hChildStdoutWr);
	CloseHandle(pi.hThread);

	auto deadline = std::chrono::steady_clock::now() + timeout;
	std::string result;
	char buf[1024];
	DWORD read = 0;

	while (true) {
		auto remain =
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
		if (remain <= 0) {
			TerminateProcess(pi.hProcess, 1);
			result += "\n[TIMEOUT]";
			log::warn("[agent-reach] exec timeout after " + std::to_string(timeout.count()) +
					  "s: " + std::string(cmd).substr(0, 80));
			break;
		}

		DWORD wait_ms = static_cast<DWORD>(std::min<int64_t>(remain, 500));
		DWORD wait_rc = WaitForSingleObject(hChildStdoutRd, wait_ms);
		if (wait_rc == WAIT_TIMEOUT)
			continue;
		if (wait_rc != WAIT_OBJECT_0)
			break;

		if (!ReadFile(hChildStdoutRd, buf, sizeof(buf) - 1, &read, nullptr) || read == 0)
			break;
		buf[read] = '\0';
		result += buf;
	}

	CloseHandle(hChildStdoutRd);
	WaitForSingleObject(pi.hProcess, 5000);
	CloseHandle(pi.hProcess);

	while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
		result.pop_back();
	return result;
#else
	std::ostringstream ws;
	ws << "timeout " << timeout.count() << " sh -c " << std::quoted(full_cmd);
	std::string wrapped = ws.str();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(wrapped.c_str(), "r"), pclose);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
	if (!pipe)
		return {};
	std::string result;
	char buf[1024];
	while (fgets(buf, sizeof(buf), pipe.get()))
		result += buf;
	while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
		result.pop_back();
	return result;
#endif
}

// ─── command_available ────────────────────────────────────────────────────

bool client::agent_reach_client::command_available(std::string_view name) {
#ifdef _WIN32
	std::string cmd = "where ";
	cmd += name;
	cmd += " >nul 2>&1";
	int rc = std::system(cmd.c_str());
	log::info(std::string("[agent-reach] where ") + std::string(name) + " -> exit code " + std::to_string(rc));

	if (rc == 0)
		return true;

	if (auto* path = std::getenv("PATH"))
		log::info(std::string("[agent-reach] PATH=") + path);

	std::string name_exe(name);
	name_exe += ".exe";
	std::string name_cmd(name);
	name_cmd += ".cmd";

	auto try_path = [&](std::string const& dir) -> bool {
		std::filesystem::path p(dir);
		if (!std::filesystem::exists(p))
			return false;
		bool ok = std::filesystem::exists(p / name_exe) || std::filesystem::exists(p / name_cmd);
		if (ok)
			log::info(std::string("[agent-reach] found ") + std::string(name) + " via fallback: " + dir);
		return ok;
	};

	if (auto* home = std::getenv("USERPROFILE")) {
		std::string home_s(home);
		log::info(std::string("[agent-reach] USERPROFILE=") + home_s);
		if (try_path(home_s + "\\python\\Scripts"))
			return true;
		if (try_path(home_s + "\\AppData\\Local\\Programs\\Python\\Python313\\Scripts"))
			return true;
		if (try_path(home_s + "\\AppData\\Local\\Programs\\Python\\Python312\\Scripts"))
			return true;
		if (try_path(home_s + "\\scoop\\shims"))
			return true;
	} else {
		log::info("[agent-reach] USERPROFILE is NULL");
	}
	if (auto* appdata = std::getenv("APPDATA")) {
		std::string ad(appdata);
		log::info(std::string("[agent-reach] APPDATA=") + ad);
		if (try_path(ad + "\\npm"))
			return true;
	} else {
		log::info("[agent-reach] APPDATA is NULL");
	}
	if (auto* pf = std::getenv("ProgramFiles")) {
		std::string pf_s(pf);
		log::info(std::string("[agent-reach] ProgramFiles=") + pf_s);
		if (try_path(pf_s + "\\GitHub CLI"))
			return true;
	}
	if (auto* pf86 = std::getenv("ProgramFiles(x86)")) {
		if (try_path(std::string(pf86) + "\\GitHub CLI"))
			return true;
	}

	log::info(std::string("[agent-reach] ") + std::string(name) + " not found — all fallback paths exhausted");
	return false;
#else
	std::string cmd = "command -v ";
	cmd += name;
	cmd += " >/dev/null 2>&1";
	return std::system(cmd.c_str()) == 0;
#endif
}
