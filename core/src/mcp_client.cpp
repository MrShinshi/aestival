/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "mcp_client.h"
#include "log.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

// ─── helpers ───────────────────────────────────────────────────────────────

constexpr size_t kMaxToolContent = 8000;

// Truncate tool output at a UTF-8-safe boundary.
std::string truncate_utf8(std::string_view s, size_t max_len) {
	if (s.size() <= max_len)
		return std::string(s);
	auto cut = s.substr(0, max_len);
	// Walk back to last complete UTF-8 sequence boundary.
	auto pos = cut.find_last_of('\n');
	if (pos != std::string::npos && pos > max_len / 2)
		return std::string(cut.substr(0, pos)) + "\n...";
	// Truncate at last valid UTF-8 lead byte or ASCII.
	size_t i = cut.size();
	while (i > 0) {
		unsigned char c = static_cast<unsigned char>(cut[i - 1]);
		if (c < 0x80 || c >= 0xC0) // ASCII or UTF-8 lead byte
			break;
		--i;
	}
	return std::string(cut.substr(0, i)) + "...";
}

} // namespace

// ─── mcp_client constructor ──────────────────────────────────────────────

client::mcp_client::mcp_client(mcp_server_config cfg) : cfg_(std::move(cfg)) {
	if (!launch()) {
		client::log::error("[mcp:" + cfg_.name + "] failed to launch subprocess");
		return;
	}
	if (!perform_handshake()) {
		client::log::error("[mcp:" + cfg_.name + "] handshake failed");
		terminate();
		return;
	}
	connected_ = true;
	client::log::info("[mcp:" + cfg_.name + "] connected to " + server_name_ + " " + server_version_ + " — " +
					  std::to_string(tools_.size()) + " tools");
}

client::mcp_client::~mcp_client() {
	terminate();
}

// ─── launch (Windows) ──────────────────────────────────────────────────────

#ifdef _WIN32

bool client::mcp_client::launch() {
	// Create pipes
	SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

	HANDLE h_child_stdin_rd = nullptr, h_child_stdin_wr = nullptr;
	HANDLE h_child_stdout_rd = nullptr, h_child_stdout_wr = nullptr;

	if (!CreatePipe(&h_child_stdin_rd, &h_child_stdin_wr, &sa, 0))
		return false;
	if (!CreatePipe(&h_child_stdout_rd, &h_child_stdout_wr, &sa, 0))
		return false;

	// Child inherits its ends; parent does NOT inherit child's ends.
	SetHandleInformation(h_child_stdin_wr, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(h_child_stdout_rd, HANDLE_FLAG_INHERIT, 0);

	// Build command line
	std::wstring cmd_line = L"cmd.exe /c \"";

	// Prepend PATH entries (same pattern as agent_reach_client)
	cmd_line += L"set \"PATH=";
	if (auto* home = std::getenv("USERPROFILE"))
		cmd_line += std::wstring(home, home + strlen(home)) + L"\\python\\Scripts;";
	if (auto* appdata = std::getenv("APPDATA"))
		cmd_line += std::wstring(appdata, appdata + strlen(appdata)) + L"\\npm;";
	cmd_line += L"%PATH%\" && ";

		// Inject per-server environment variables
		for (auto const& [k, v] : cfg_.env) {
			std::wstring wk(k.begin(), k.end());
			std::wstring wv(v.begin(), v.end());
			cmd_line += L"set \"" + wk + L"=" + wv + L"\" && ";
		}

	// Append command + args
	{
		std::wstring cmd(cfg_.command.begin(), cfg_.command.end());
		cmd_line += cmd;
	}
	for (auto const& a : cfg_.args) {
		cmd_line += L" ";
		std::wstring arg(a.begin(), a.end());
		cmd_line += arg;
	}
	cmd_line += L"\"";

	// Build environment block for CREATE_NO_WINDOW
	STARTUPINFOW si = {sizeof(STARTUPINFOW)};
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = h_child_stdin_rd;
	si.hStdOutput = h_child_stdout_wr;
	si.hStdError = h_child_stdout_wr;

	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
						&pi)) {
		CloseHandle(h_child_stdin_rd);
		CloseHandle(h_child_stdin_wr);
		CloseHandle(h_child_stdout_rd);
		CloseHandle(h_child_stdout_wr);
		return false;
	}

	// Close child-side ends
	CloseHandle(h_child_stdin_rd);
	CloseHandle(h_child_stdout_wr);

	// Store parent-side ends
	h_stdin_wr_ = h_child_stdin_wr;
	h_stdout_rd_ = h_child_stdout_rd;
	h_process_ = pi.hProcess;
	h_thread_ = pi.hThread;

	return true;
}

void client::mcp_client::terminate() {
	if (h_stdin_wr_) {
		CloseHandle(h_stdin_wr_);
		h_stdin_wr_ = nullptr;
	}
	if (h_stdout_rd_) {
		CloseHandle(h_stdout_rd_);
		h_stdout_rd_ = nullptr;
	}
	if (h_process_) {
		// Give the child a chance to exit gracefully (stdin close signals EOF).
		if (WaitForSingleObject(h_process_, 2000) == WAIT_TIMEOUT)
			TerminateProcess(h_process_, 1);
		CloseHandle(h_process_);
		h_process_ = nullptr;
	}
	if (h_thread_) {
		CloseHandle(h_thread_);
		h_thread_ = nullptr;
	}
}

// ─── launch (Unix) ─────────────────────────────────────────────────────────

#else

bool client::mcp_client::launch() {
	int stdin_pipe[2], stdout_pipe[2];
	if (pipe(stdin_pipe) != 0)
		return false;
	if (pipe(stdout_pipe) != 0) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		return false;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		return false;
	}

	if (pid == 0) {
		// ── child ────────────────────────────────────────────────────────
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		dup2(stdout_pipe[1], STDERR_FILENO); // stderr → same pipe for simplicity

		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);


		// Inject per-server environment variables
		for (auto const& [k, v] : cfg_.env)
			setenv(k.c_str(), v.c_str(), 1);
		std::vector<char*> argv;
		argv.push_back(const_cast<char*>(cfg_.command.c_str()));
		for (auto const& a : cfg_.args)
			argv.push_back(const_cast<char*>(a.c_str()));
		argv.push_back(nullptr);

		execvp(cfg_.command.c_str(), argv.data());
		_exit(127); // exec failed
	}

	// ── parent ───────────────────────────────────────────────────────────
	close(stdin_pipe[0]);
	close(stdout_pipe[1]);

	child_pid_ = pid;
	stdin_fd_ = stdin_pipe[1];
	stdout_fd_ = stdout_pipe[0];
	return true;
}

void client::mcp_client::terminate() {
	if (stdin_fd_ >= 0) {
		close(stdin_fd_);
		stdin_fd_ = -1;
	}
	if (stdout_fd_ >= 0) {
		close(stdout_fd_);
		stdout_fd_ = -1;
	}
	if (child_pid_ > 0) {
		// Give the child a chance to exit gracefully.
		int status = 0;
		int waited = 0;
		while (waited < 20) { // 2 seconds
			pid_t rc = waitpid(child_pid_, &status, WNOHANG);
			if (rc > 0)
				break;
			usleep(100000);
			waited++;
		}
		if (waited >= 20)
			kill(child_pid_, SIGTERM);
		child_pid_ = -1;
	}
}

#endif

// ─── JSON-RPC framing ─────────────────────────────────────────────────────

void client::mcp_client::write_line(nlohmann::json const& j) {
	std::string payload = j.dump() + "\n";
#ifdef _WIN32
	DWORD written = 0;
	if (!WriteFile(h_stdin_wr_, payload.c_str(), static_cast<DWORD>(payload.size()), &written, nullptr))
		throw std::runtime_error("[mcp:" + cfg_.name + "] write failed");
#else
	if (write(stdin_fd_, payload.c_str(), payload.size()) < 0)
		throw std::runtime_error("[mcp:" + cfg_.name + "] write failed: " + std::string(strerror(errno)));
#endif
}

std::string client::mcp_client::read_line() {
	std::string line;
	char ch;
	auto deadline = std::chrono::steady_clock::now() + cfg_.call_timeout;

	while (true) {
		auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
						  .count();
		if (remain <= 0)
			throw std::runtime_error("[mcp:" + cfg_.name + "] read timeout");

#ifdef _WIN32
		DWORD wait_ms = static_cast<DWORD>(std::min<int64_t>(remain, 500));
		DWORD wait_rc = WaitForSingleObject(h_stdout_rd_, wait_ms);
		if (wait_rc == WAIT_TIMEOUT)
			continue;
		if (wait_rc != WAIT_OBJECT_0)
			break;

		DWORD bytes_read = 0;
		if (!ReadFile(h_stdout_rd_, &ch, 1, &bytes_read, nullptr) || bytes_read == 0)
			break;
#else
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(stdout_fd_, &fds);
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 500000; // 500ms
		int rc = select(stdout_fd_ + 1, &fds, nullptr, nullptr, &tv);
		if (rc < 0)
			throw std::runtime_error("[mcp:" + cfg_.name + "] select failed: " + std::string(strerror(errno)));
		if (rc == 0)
			continue;

		ssize_t n = read(stdout_fd_, &ch, 1);
		if (n <= 0)
			break;
#endif
		if (ch == '\n')
			break;
		line += ch;
	}
	if (line.empty())
		throw std::runtime_error("[mcp:" + cfg_.name + "] connection closed");
	return line;
}

// ─── send_request / send_notification ─────────────────────────────────────

nlohmann::json client::mcp_client::send_request(std::string_view method, nlohmann::json const& params) {
	std::lock_guard<std::mutex> lock(io_mutex_);

	int64_t id = next_id_++;
	nlohmann::json req;
	req["jsonrpc"] = "2.0";
	req["id"] = id;
	req["method"] = method;
	req["params"] = params;

	write_line(req);
	return nlohmann::json::parse(read_response(id));
}

void client::mcp_client::send_notification(std::string_view method, nlohmann::json const& params) {
	std::lock_guard<std::mutex> lock(io_mutex_);

	nlohmann::json req;
	req["jsonrpc"] = "2.0";
	req["method"] = method;
	req["params"] = params;

	write_line(req);
	// Notifications have no response — the server must not reply.
}

std::string client::mcp_client::read_response(int64_t expected_id) {
	auto deadline = std::chrono::steady_clock::now() + cfg_.call_timeout;

	while (true) {
		auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
						  .count();
		if (remain <= 0)
			throw std::runtime_error("[mcp:" + cfg_.name + "] response timeout for id=" + std::to_string(expected_id));

		std::string line;
		try {
			line = read_line();
		} catch (...) {
			throw std::runtime_error("[mcp:" + cfg_.name + "] connection lost while waiting for id=" +
									 std::to_string(expected_id));
		}

		if (line.empty())
			continue;

		auto msg = nlohmann::json::parse(line, nullptr, false);
		if (msg.is_discarded()) {
			client::log::warn("[mcp:" + cfg_.name + "] unparseable line: " + line.substr(0, 100));
			continue;
		}

		// Check for JSON-RPC error
		if (msg.contains("error")) {
			auto err = msg["error"];
			int code = err.value("code", 0);
			std::string message = err.value("message", "unknown error");
			throw std::runtime_error("[mcp:" + cfg_.name + "] error " + std::to_string(code) + ": " + message);
		}

		int64_t id = msg.value("id", int64_t(-1));
		if (id == expected_id && msg.contains("result"))
			return msg["result"].dump();

		// Notifications or server→client requests — skip.
		if (id == -1)
			continue;

		client::log::warn("[mcp:" + cfg_.name + "] unexpected id=" + std::to_string(id) + " (expected " +
						  std::to_string(expected_id) + ")");
	}
}

// ─── handshake ────────────────────────────────────────────────────────────

bool client::mcp_client::perform_handshake() {
	try {
		// Step 1: initialize
		nlohmann::json init_params;
		init_params["protocolVersion"] = "2024-11-05";
		init_params["capabilities"] = nlohmann::json::object();
		init_params["clientInfo"]["name"] = "aestival";
		init_params["clientInfo"]["version"] = "1.0.0";

		auto init_result = send_request("initialize", init_params);

		server_name_ = init_result.value("serverInfo", nlohmann::json::object()).value("name", cfg_.name);
		server_version_ = init_result.value("serverInfo", nlohmann::json::object()).value("version", "unknown");

		// Step 2: send initialized notification
		send_notification("notifications/initialized", nlohmann::json::object());

		// Step 3: discover tools
		auto tools_result = send_request("tools/list", nlohmann::json::object());

		auto const& tools_arr = tools_result["tools"];
		if (!tools_arr.is_array()) {
			client::log::warn("[mcp:" + cfg_.name + "] tools/list returned non-array");
			return true; // empty tools is valid
		}

		for (auto const& t : tools_arr)
			tools_.push_back(convert_tool(t));

		return true;
	} catch (std::exception const& ex) {
		client::log::error("[mcp:" + cfg_.name + "] handshake failed: " + std::string(ex.what()));
		return false;
	}
}

// ─── tool_provider interface ──────────────────────────────────────────────

std::vector<client::tool_definition> client::mcp_client::get_tools() const {
	return tools_;
}

std::string client::mcp_client::execute_tool(std::string_view tool_name, nlohmann::json const& args) {
	if (!connected_.load())
		return "Error: MCP server '" + cfg_.name + "' is not connected";

	try {
		nlohmann::json params;
		params["name"] = tool_name;
		params["arguments"] = args;

		auto result = send_request("tools/call", params);

		std::string content = extract_content(result);
		if (content.empty())
			return "(tool returned no content)";
		return truncate_utf8(content, kMaxToolContent);
	} catch (std::exception const& ex) {
		connected_.store(false);
		return "Error: MCP tool '" + std::string(tool_name) + "' failed: " + ex.what();
	}
}

// ─── MCP ↔ aestival conversion ────────────────────────────────────────────

client::tool_definition client::mcp_client::convert_tool(nlohmann::json const& mcp_tool) {
	tool_definition td;
	td.name = mcp_tool.value("name", "");
	td.description = mcp_tool.value("description", "");

	// MCP uses "inputSchema", OpenAI uses "parameters" — same JSON Schema.
	auto schema = mcp_tool.find("inputSchema");
	if (schema != mcp_tool.end())
		td.parameters = *schema;
	else
		td.parameters = {{"type", "object"}, {"properties", nlohmann::json::object()}};

	return td;
}

std::string client::mcp_client::extract_content(nlohmann::json const& result) {
	auto const& content = result["content"];
	if (!content.is_array())
		return {};

	std::string text;
	for (auto const& block : content) {
		if (block.value("type", "") == "text") {
			auto const& t = block["text"];
			if (t.is_string())
				text += t.get<std::string>();
		}
	}
	return text;
}
