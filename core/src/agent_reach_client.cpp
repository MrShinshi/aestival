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

// ─── helpers ─────────────────────────────────────────────────────────────

// Strip HTML tags + entities + collapse whitespace.
std::string plain_text(std::string_view s) {
	struct entity {
		std::string_view raw;
		char decoded;
	};
	static constexpr entity kEntities[] = {
		{"&amp;", '&'},	 {"&lt;", '<'},	   {"&gt;", '>'},	{"&quot;", '"'},
		{"&#39;", '\''}, {"&#x27;", '\''}, {"&nbsp;", ' '}, {"&#x2F;", '/'},
	};
	std::string r;
	r.reserve(s.size());
	bool in_tag = false;
	for (size_t i = 0; i < s.size();) {
		char const c = s[i];
		if (c == '<') {
			in_tag = true;
			++i;
			continue;
		}
		if (in_tag && c == '>') {
			in_tag = false;
			++i;
			continue;
		}
		if (in_tag) {
			++i;
			continue;
		}
		if (c == '&') {
			bool matched = false;
			for (auto const& e : kEntities) {
				if (s.size() - i >= e.raw.size() && s.substr(i, e.raw.size()) == e.raw) {
					r += e.decoded;
					i += e.raw.size();
					matched = true;
					break;
				}
			}
			if (matched)
				continue;
			if (i + 2 < s.size() && s[i + 1] == '#') {
				auto end = s.find(';', i + 2);
				if (end != std::string::npos && end - i <= 7) {
					i = end + 1;
					continue;
				}
			}
		}
		r += s[i++];
	}
	std::string out;
	out.reserve(r.size());
	bool space = true;
	for (char ch : r) {
		if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
			if (!space)
				out += ' ';
			space = true;
		} else {
			out += ch;
			space = false;
		}
	}
	if (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

std::string clip(std::string_view s, size_t max_len = 800) {
	if (s.size() <= max_len)
		return std::string(s);
	auto cut = s.substr(0, max_len);
	auto pos = cut.rfind(' ');
	if (pos != std::string::npos && pos > max_len / 2)
		cut = cut.substr(0, pos);
	return std::string(cut) + "...";
}

// ─── shell_escape ───────────────────────────────────────────────────────
// Escape a string for insertion inside a double-quoted shell argument.
// This is distinct from std::quoted (which wraps + escapes): shell_escape
// does NOT add outer quotes — the caller provides those.  Used only when
// the value must be embedded inside an already-quoted shell token (e.g.
// the JSON-like argument to mcporter call).
std::string shell_escape(std::string_view s) {
	std::string r;
	r.reserve(s.size());
	for (char c : s) {
		switch (c) {
		case '"':
			r += "\\\"";
			break;
		case '\\':
			r += "\\\\";
			break;
		case '$':
			r += "\\$";
			break;
		case '`':
			r += "\\`";
			break;
		case '!':
			r += "\\!";
			break;
		case '|':
			r += "\\|";
			break;
		case '&':
			r += "\\&";
			break; // cmd.exe command separator
		case '^':
			r += "\\^";
			break; // cmd.exe escape char
		case '\n':
		case '\r':
			r += ' ';
			break; // replace, don't drop
		default:
			r += c;
			break;
		}
	}
	return r;
}

// ─── mcporter JSON parser ───────────────────────────────────────────────

std::string parse_mcporter_json(std::string_view json) {
	auto j = nlohmann::json::parse(json, nullptr, false);
	if (j.is_discarded())
		return {};

	auto content = j.value("content", nlohmann::json::array());
	if (!content.is_array() || content.empty())
		return {};

	std::string text;
	for (auto const& block : content)
		if (block.value("type", "") == "text")
			text += block.value("text", "");

	if (text.empty())
		return {};
	constexpr size_t kMaxChars = 2000;
	if (text.size() > kMaxChars) {
		text.resize(kMaxChars);
		auto pos = text.rfind('\n');
		if (pos != std::string::npos && pos > kMaxChars / 2)
			text.resize(pos);
		text += "\n...";
	}
	return text;
}

} // namespace

// ─── agent_reach_capabilities::summary ───────────────────────────────────

std::string client::agent_reach_capabilities::summary() const {
	std::ostringstream s;
	s << "exa=" << exa_search << " web=" << web_fetch << " v2ex=" << v2ex << " bili=" << bilibili << " gh=" << github
	  << " twitter=" << twitter;
	return s.str();
}

// ─── agent_reach_client ──────────────────────────────────────────────────

client::agent_reach_client::agent_reach_client(bool verify_tls) : verify_tls_(verify_tls) {
	probe();
}

client::agent_reach_capabilities client::agent_reach_client::probe() {
	caps_ = {};
	caps_.web_fetch = true; // curl always available
	caps_.v2ex = true;		// curl always available
	caps_.exa_search = command_available("mcporter");
	caps_.bilibili = command_available("bili");
	caps_.github = command_available("gh");
	caps_.twitter = command_available("twitter");
	caps_.any_search = caps_.exa_search || caps_.web_fetch;
	probed_ = true;

	log::info("[agent-reach] probe: " + caps_.summary());
	return caps_;
}

// ─── exec (P0-1 fix: timeout via watchdog) ─────────────────────────────

std::string client::agent_reach_client::exec(std::string_view cmd, std::chrono::seconds timeout) {
	std::string full_cmd(cmd);
	full_cmd += " 2>&1";

#ifdef _WIN32
	// On Windows, popen has no built-in timeout.  We use a watchdog thread
	// that terminates the child process if the timeout expires.
	struct pipe_guard {
		FILE* f = nullptr;
		~pipe_guard() {
			if (f)
				_pclose(f);
		}
	};

	// Launch the child process via CreateProcess to get its PID.
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

	// Prepend known install dirs to PATH so child cmd.exe can find bili/gh/etc.
	// The bot process may have a stripped PATH (e.g. launched from PS without
	// user PATH entries).  These dirs were validated by probe() already.
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

	// Escape cmd.exe metacharacters to prevent command injection.
	// `cmd.exe /c` interprets & | < > ^ % as special; we escape them
	// with ^ (cmd escape) and double %.
	static auto escape_cmd_meta = [](std::string const& s) -> std::string {
		std::string r;
		r.reserve(s.size() * 2);
		for (char ch : s) {
			switch (ch) {
			case '&':
			case '|':
			case '<':
			case '>':
			case '^':
				r += '^';
				break;
			case '%':
				r += '%';
				break; // %% escapes % in cmd
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

	// Read with timeout.
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

		// Wait up to 500ms for data.
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
	// Unix: use alarm() + signal, or simply rely on curl --max-time.
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

// ─── command_available ──────────────────────────────────────────────────

bool client::agent_reach_client::command_available(std::string_view name) {
#ifdef _WIN32
	std::string cmd = "where ";
	cmd += name;
	cmd += " >nul 2>&1";
	int rc = std::system(cmd.c_str());
	log::info(std::string("[agent-reach] where ") + std::string(name) + " -> exit code " + std::to_string(rc));

	if (rc == 0)
		return true;

	// where failed — dump env for diagnosis
	if (auto* path = std::getenv("PATH"))
		log::info(std::string("[agent-reach] PATH=") + path);

	// Fallback: check known install paths directly.
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

	// pip user scripts
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
	// npm global
	if (auto* appdata = std::getenv("APPDATA")) {
		std::string ad(appdata);
		log::info(std::string("[agent-reach] APPDATA=") + ad);
		if (try_path(ad + "\\npm"))
			return true;
	} else {
		log::info("[agent-reach] APPDATA is NULL");
	}
	// GitHub CLI
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

// ─── search_exa ─────────────────────────────────────────────────────────
// Matches: mcporter call 'exa.web_search_exa(query:"...",numResults:N)' --output json

std::string client::agent_reach_client::search_exa(std::string_view query, int num_results) {
	if (!caps_.exa_search)
		return {};
	auto safe_q = shell_escape(query);
	std::ostringstream cmd;
	cmd << "mcporter call 'exa.web_search_exa(query: \"" << safe_q << "\", numResults: " << num_results
		<< ")' --output json";
	auto raw = exec(cmd.str(), std::chrono::seconds(30));
	if (raw.empty()) {
		log::warn("[agent-reach] Exa returned empty");
		return {};
	}
	return parse_mcporter_json(raw);
}

// ─── fetch_page (Jina Reader) ───────────────────────────────────────────
// Matches: curl -s "https://r.jina.ai/URL"

std::string client::agent_reach_client::fetch_page(std::string_view url) {
	std::string rurl = "https://r.jina.ai/";
	rurl += url;
	std::ostringstream cmd;
	cmd << "curl -s --max-time 15 " << std::quoted(rurl);
	return clip(exec(cmd.str()), 1500);
}

// ─── search_v2ex (P2-3 fix: try node API first) ────────────────────────
// Matches: curl -s "https://www.v2ex.com/api/topics/hot.json"

std::string client::agent_reach_client::search_v2ex(std::string_view query) {
	auto q = client::trim(std::string(query));
	std::string raw;

	// If query looks like a node name (single word, no spaces), try node API first.
	if (!q.empty() && q.find(' ') == std::string::npos && q.size() <= 20) {
		raw = exec("curl -s --max-time 10 "
				   "\"https://www.v2ex.com/api/topics/show.json?node_name=" +
				   client::url_encode(q) + "&page=1\" -H \"User-Agent: agent-reach/1.0\"");
	}

	// Fallback: hot topics filtered client-side.
	if (raw.empty() || raw == "[]") {
		raw = exec("curl -s --max-time 10 "
				   "\"https://www.v2ex.com/api/topics/hot.json\" "
				   "-H \"User-Agent: agent-reach/1.0\"");
	}

	if (raw.empty())
		return {};

	auto j = nlohmann::json::parse(raw, nullptr, false);
	if (j.is_discarded() || !j.is_array())
		return {};

	std::ostringstream out;
	int count = 0;
	for (auto const& t : j) {
		if (count >= 5)
			break;
		auto title = plain_text(t.value("title", ""));
		auto node = t.value("node", nlohmann::json::object());
		auto node_t = node.value("title", "");
		auto replies = t.value("replies", 0);
		auto url = t.value("url", "");
		if (title.empty())
			continue;
		if (!q.empty() && q.find(' ') != std::string::npos) {
			auto lt = title, lq = q;
			std::transform(lt.begin(), lt.end(), lt.begin(), ::tolower);
			std::transform(lq.begin(), lq.end(), lq.begin(), ::tolower);
			if (lt.find(lq) == std::string::npos)
				continue;
		}
		++count;
		out << "[" << count << "] " << title << "\n"
			<< "    节点: " << node_t << " | 回复: " << replies << "\n";
		if (!url.empty())
			out << "    https://www.v2ex.com" << url << "\n";
		out << "\n";
	}
	if (count == 0)
		return {};
	return out.str();
}

// ─── search_bilibili ────────────────────────────────────────────────────
// Matches: bili search "query" --type video -n 5

std::string client::agent_reach_client::search_bilibili(std::string_view query, int max_results) {
	if (!caps_.bilibili)
		return {};
	std::ostringstream cmd;
	cmd << "bili search " << std::quoted(std::string(query)) << " --type video -n " << max_results;
	auto raw = exec(cmd.str());
	if (raw.empty())
		return {};

	std::istringstream in(raw);
	std::ostringstream out;
	std::string line;
	int count = 0;
	while (std::getline(in, line) && count < max_results) {
		line = client::trim(line);
		if (line.empty())
			continue;
		++count;
		out << "[" << count << "] " << line << "\n";
	}
	if (count == 0)
		return {};
	return out.str();
}

// ─── search_github ──────────────────────────────────────────────────────
// Matches: gh search repos "query" --sort stars --limit 5

std::string client::agent_reach_client::search_github(std::string_view query, int max_results) {
	if (!caps_.github)
		return {};
	std::ostringstream cmd;
	cmd << "gh search repos " << std::quoted(std::string(query)) << " --sort stars --limit " << max_results
		<< " --json name,description,stargazersCount,url 2>&1";
	auto raw = exec(cmd.str());
	if (raw.empty())
		return {};

	auto j = nlohmann::json::parse(raw, nullptr, false);
	if (j.is_discarded() || !j.is_array())
		return {};

	std::ostringstream out;
	int count = 0;
	for (auto const& r : j) {
		if (count >= max_results)
			break;
		++count;
		out << "[" << count << "] **" << r.value("name", "") << "**"
			<< " \xF0\x9F\x8C\x9F " << r.value("stargazersCount", 0) << "\n"
			<< "    " << r.value("description", "") << "\n"
			<< "    " << r.value("url", "") << "\n\n";
	}
	if (count == 0)
		return {};
	return out.str();
}

// ─── search_twitter ─────────────────────────────────────────────────────
// Matches: twitter search "query" -n 5

std::string client::agent_reach_client::search_twitter(std::string_view query, int max_results) {
	if (!caps_.twitter)
		return {};
	std::ostringstream cmd;
	cmd << "twitter search " << std::quoted(std::string(query)) << " -n " << max_results;
	auto raw = exec(cmd.str());
	if (raw.empty())
		return {};

	std::istringstream in(raw);
	std::ostringstream out;
	std::string line;
	int count = 0;
	while (std::getline(in, line) && count < max_results) {
		line = client::trim(line);
		if (line.empty())
			continue;
		++count;
		out << "[" << count << "] " << clip(line, 300) << "\n";
	}
	if (count == 0)
		return {};
	return out.str();
}

// ─── bili_hot ─────────────────────────────────────────────────────────
// Matches: bili hot -n N

std::string client::agent_reach_client::bili_hot(int n) {
	if (!caps_.bilibili)
		return {};
	std::ostringstream cmd;
	cmd << "bili hot -n " << n;
	auto raw = exec(cmd.str());
	if (raw.empty())
		return {};

	std::istringstream in(raw);
	std::ostringstream out;
	std::string line;
	int count = 0;
	while (std::getline(in, line) && count < n) {
		line = client::trim(line);
		if (line.empty())
			continue;
		++count;
		out << "[" << count << "] " << line << "\n";
	}
	return count == 0 ? std::string{} : out.str();
}

// ─── search_wikipedia ────────────────────────────────────────────────────
// Zero-config fallback: Wikipedia API via curl.
// Matches: curl -s "...wikipedia.org/w/api.php?...srsearch=QUERY..."

std::string client::agent_reach_client::search_wikipedia(std::string_view query, int num_results) {
	std::ostringstream cmd;
	cmd << "curl -s --max-time 10 "
		<< "\"https://en.wikipedia.org/w/api.php?"
		<< "action=query&list=search&srsearch=" << client::url_encode(query) << "&format=json&srlimit=" << num_results
		<< "\"";
	auto raw = exec(cmd.str());
	if (raw.empty())
		return {};

	auto j = nlohmann::json::parse(raw, nullptr, false);
	if (j.is_discarded())
		return {};

	auto const& qr = j["query"];
	if (!qr.is_object())
		return {};
	auto const& search = qr["search"];
	if (!search.is_array() || search.empty())
		return {};

	std::ostringstream out;
	int count = 0;
	constexpr size_t kMaxSnippet = 300;
	for (auto const& r : search) {
		if (count >= num_results)
			break;
		auto title = plain_text(r.value("title", ""));
		auto snippet = plain_text(r.value("snippet", ""));
		if (snippet.size() > kMaxSnippet) {
			snippet.resize(kMaxSnippet);
			auto pos = snippet.rfind(' ');
			if (pos != std::string::npos && pos > kMaxSnippet / 2)
				snippet.resize(pos);
			snippet += "...";
		}
		++count;
		out << "[" << count << "] **" << title << "**\n"
			<< "    " << snippet << "\n\n";
	}
	return count == 0 ? std::string{} : out.str();
}
