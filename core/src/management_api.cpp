/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "management_api.h"
#include "agent_controller.h"
#include "agent_instance.h"
#include "system_monitor.h"
#include "agent_registry.h"
#include "bot_config.h"
#include "log.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <sstream>

namespace client {

using tcp = boost::asio::ip::tcp;
namespace beast = boost::beast;
namespace http = beast::http;

// ─── helpers ────────────────────────────────────────────────────────────────

namespace {
	// Maximum lengths for user-supplied string fields.
	constexpr size_t k_max_agent_id = 64;
	constexpr size_t k_max_agent_name = 128;
	constexpr size_t k_max_app_id = 256;
	constexpr size_t k_max_api_key = 512;
	constexpr size_t k_max_path = 1024;
	constexpr size_t k_max_body = 65536;  // 64 KiB

	// Allowed characters in agent IDs.
	bool is_valid_agent_id(std::string_view id) {
		if (id.empty() || id.size() > k_max_agent_id)
			return false;
		for (char c : id)
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
				return false;
		return true;
	}

	// Reject path components that traverse outside the intended directory.
	bool is_safe_path_component(std::string_view path) {
		if (path.empty() || path.size() > k_max_path)
			return false;
		// Disallow absolute paths, traversal sequences, and null bytes.
		if (path[0] == '/' || path[0] == '\\')
			return false;
		if (path.find("..") != std::string_view::npos)
			return false;
		if (path.find('\0') != std::string_view::npos)
			return false;
		return true;
	}

	// Truncate a string to max_len, returning the original if within limit.
	std::string truncate_str(std::string s, size_t max_len) {
		if (s.size() > max_len)
			s.resize(max_len);
		return s;
	}

	// Allowed platform and LLM provider values.
	bool is_valid_platform(std::string_view p) {
		return p == "qq" || p == "console";
	}
	bool is_valid_llm_provider(std::string_view p) {
		return p == "deepseek" || p == "openai";
	}

	// Allowed conversation ID characters.
	bool is_valid_conversation_id(std::string_view id) {
		if (id.empty() || id.size() > k_max_path)
			return false;
		for (char c : id) {
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':'))
				return false;
		}
		if (id.find("..") != std::string_view::npos)
			return false;
		if (id.find('\0') != std::string_view::npos)
			return false;
		return true;
	}
} // namespace

static std::string json_response(http::status status, nlohmann::json const& body) {
	auto j = nlohmann::json::object();
	j["status"] = status == http::status::ok ? "ok" : "error";
	if (body.is_object())
		for (auto const& [k, v] : body.items())
			j[k] = v;
	else
		j["data"] = body;
	return j.dump();
}

static std::string error_response(http::status /*status*/, std::string_view msg) {
	auto j = nlohmann::json::object({{"status", "error"}, {"error", std::string(msg)}});
	return j.dump();
}

// ─── impl ───────────────────────────────────────────────────────────────────

struct management_api::impl {
	agent_registry& registry;
	global_config const& config;
	mgmt::jwt_verifier jwt;
	std::chrono::steady_clock::time_point started_at;

	// Cached system resource snapshot (refreshed at most every 2 seconds)
	mutable std::chrono::steady_clock::time_point last_snapshot_ts_{};
	mutable system_resource_snapshot cached_snapshot_;
	system_resource_snapshot get_system_snapshot() const {
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_snapshot_ts_).count();
		if (elapsed >= 2 || elapsed < 0) {
			cached_snapshot_ = collect_system_resources();
			last_snapshot_ts_ = now;
		}
		return cached_snapshot_;
	}

	boost::asio::io_context ioc;
	tcp::acceptor acceptor;
	std::thread worker;
	std::mutex lifecycle_mutex_; // protects start/stop transitions

	impl(agent_registry& r, global_config const& g)
		: registry(r), config(g), jwt(g.jwt_secret), acceptor(ioc) {}

	// ── JWT auth helper ──────────────────────────────────────────────────
	// Returns the authenticated username or throws.

	std::string authenticate(http::request<http::string_body> const& req) {
		auto auth_hdr = req.find(http::field::authorization);
		if (auth_hdr == req.end())
			throw std::runtime_error("missing Authorization header");

		std::string_view val = auth_hdr->value();
		static constexpr std::string_view k_bearer = "Bearer ";
		if (val.size() <= k_bearer.size() || !val.starts_with(k_bearer))
			throw std::runtime_error("invalid Authorization header");

		return jwt.verify(val.substr(k_bearer.size()));
	}

	// ── route dispatcher ─────────────────────────────────────────────────

	http::response<http::string_body> handle(http::request<http::string_body> req) {
		auto target = std::string(req.target());
		auto method = req.method();

		try {
			// Health check — no auth required
			if (target == "/api/v1/health" && method == http::verb::get)
				return handle_health();

			// All other endpoints require auth
			std::string user;
			try {
				user = authenticate(req);
			} catch (std::exception const& ex) {
				return make_response(http::status::unauthorized,
									 error_response(http::status::unauthorized, ex.what()));
			}

			// ── Agent routes ──────────────────────────────────────────────
			// Strict routing: match exact paths to avoid overly broad matches.
			if (target == "/api/v1/agents" && method == http::verb::get)
				return handle_agents_list();
			if (target == "/api/v1/agents" && method == http::verb::post)
				return handle_agents_create(req.body());
			if (target.starts_with("/api/v1/agents/") && target.ends_with("/start") &&
				method == http::verb::post)
				return handle_agent_action(extract_id(target, "/start"), "start");
			if (target.starts_with("/api/v1/agents/") && target.ends_with("/stop") &&
				method == http::verb::post)
				return handle_agent_action(extract_id(target, "/stop"), "stop");
			if (target.starts_with("/api/v1/agents/") && target.ends_with("/config") &&
				method == http::verb::put)
				return handle_agent_config(extract_id(target, "/config"), req.body());
			if (target.starts_with("/api/v1/agents/") && method == http::verb::delete_) {
				// Must be exactly /api/v1/agents/{id} — no extra segments.
				auto id = extract_id_raw(target);
				if (!id.empty() && is_valid_agent_id(id))
					return handle_agent_remove(id);
			}

			// ── Log routes ───────────────────────────────────────────────
			if (target.starts_with("/api/v1/logs") && method == http::verb::get)
				return handle_logs(target);

			// ── Conversation routes ──────────────────────────────────────
			if (target == "/api/v1/conversations" && method == http::verb::get)
				return handle_conversations_list();
			if (target.starts_with("/api/v1/conversations/") && method == http::verb::get) {
				auto id = extract_conversation_id(target);
				if (!id.empty())
					return handle_conversation_detail(id);
			}

			// ── Metrics routes ─────────────────────────────────────────
			if (target == "/api/v1/metrics" && method == http::verb::get)
				return handle_metrics();
			if (target.starts_with("/api/v1/agents/") && target.ends_with("/metrics") &&
				method == http::verb::get)
				return handle_agent_metrics(extract_id(target, "/metrics"));

			// ── Token stats route ──────────────────────────────────────
			if (target == "/api/v1/tokens/stats" && method == http::verb::get)
				return handle_token_stats();

			return make_response(http::status::not_found,
								 error_response(http::status::not_found, "not found"));

		} catch (std::exception const& ex) {
			return make_response(http::status::internal_server_error,
								 error_response(http::status::internal_server_error, ex.what()));
		}
	}

	// ── route handlers ───────────────────────────────────────────────────

	http::response<http::string_body> handle_health() {
		auto j = nlohmann::json::object();
		j["status"] = "ok";
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
						   std::chrono::steady_clock::now() - started_at)
						   .count();
		j["uptime_seconds"] = elapsed;
		j["agent_count"] = registry.count();

		// Running / error agent counts
		int running = 0, errors = 0;
		auto agents = registry.list_agents();
		for (auto const& [id, st] : agents) {
			if (st == agent_status::running) ++running;
			else if (st == agent_status::error) ++errors;
		}
		j["agents_running"] = running;
		j["agents_error"] = errors;

		// System resource snapshot
		auto sys = get_system_snapshot();
		auto sj = nlohmann::json::object();
		sj["cpu_percent"] = std::round(sys.cpu_percent_recent * 10.0) / 10.0;
		sj["memory_rss_mb"] = static_cast<int64_t>(sys.memory_rss_bytes / (1024 * 1024));
		sj["memory_total_mb"] = static_cast<int64_t>(sys.system_memory_total_bytes / (1024 * 1024));
		sj["thread_count"] = sys.thread_count;
		j["system"] = std::move(sj);

		return make_response(http::status::ok, j.dump());
	}

	http::response<http::string_body> handle_agents_list() {
		auto agents = registry.list_agents();
		auto arr = nlohmann::json::array();
		for (auto const& [id, status] : agents) {
			auto* inst = registry.get_agent(id);
			auto j = nlohmann::json::object();
			j["id"] = id;
			j["status"] = std::string(to_string(status));
			if (inst) {
				j["name"] = inst->config.name;
				j["platform"] = inst->config.platform;
				j["enabled"] = inst->config.enabled;
				j["message_count"] = inst->metrics.message_count.load();
				if (!inst->bot_nick.empty())
					j["bot_nick"] = inst->bot_nick;
				if (!inst->bot_avatar.empty())
					j["bot_avatar"] = inst->bot_avatar;
				if (!inst->metrics.last_error.empty())
					j["last_error"] = inst->metrics.last_error;
			}
			arr.push_back(std::move(j));
		}
		return make_response(http::status::ok, json_response(http::status::ok, arr));
	}

	http::response<http::string_body> handle_agents_create(std::string const& body) {
		auto j = nlohmann::json::parse(body);
		agent_config cfg;

		cfg.id = truncate_str(j.value("id", ""), k_max_agent_id);
		if (!is_valid_agent_id(cfg.id))
			throw std::runtime_error("invalid agent id: must be 1-64 alphanumeric, hyphens, or underscores");
		cfg.name = truncate_str(j.value("name", ""), k_max_agent_name);
		cfg.platform = j.value("platform", "qq");
		if (!is_valid_platform(cfg.platform))
			throw std::runtime_error("invalid platform: must be 'qq' or 'console'");
		cfg.enabled = j.value("enabled", true);

		if (auto qq = j.find("qq"); qq != j.end()) {
			cfg.qq_app_id = truncate_str(qq->value("app_id", ""), k_max_app_id);
			cfg.qq_app_secret = truncate_str(qq->value("app_secret", ""), k_max_api_key);
		} else {
			// Accept flat keys from Web UI forms
			cfg.qq_app_id = truncate_str(j.value("qq_app_id", ""), k_max_app_id);
			cfg.qq_app_secret = truncate_str(j.value("qq_app_secret", ""), k_max_api_key);
		}
		cfg.llm_provider = j.value("llm_provider", "deepseek");
		if (!is_valid_llm_provider(cfg.llm_provider))
			throw std::runtime_error("invalid llm_provider: must be 'deepseek' or 'openai'");
		// Accept both flat keys (from Web UI) and nested objects (from config file)
		if (auto ds = j.find("deepseek"); ds != j.end()) {
			cfg.deepseek_api_key = truncate_str(ds->value("api_key", ""), k_max_api_key);
			cfg.deepseek_model = truncate_str(ds->value("model", "deepseek-chat"), k_max_app_id);
		} else {
			cfg.deepseek_api_key = truncate_str(j.value("deepseek_api_key", ""), k_max_api_key);
			cfg.deepseek_model = truncate_str(j.value("deepseek_model", "deepseek-chat"), k_max_app_id);
		}
		// Likewise for OpenAI
		if (auto oa = j.find("openai"); oa != j.end()) {
			cfg.openai_api_key = truncate_str(oa->value("api_key", ""), k_max_api_key);
			cfg.openai_model = truncate_str(oa->value("model", "gpt-4o"), k_max_app_id);
		} else {
			cfg.openai_api_key = truncate_str(j.value("openai_api_key", ""), k_max_api_key);
			cfg.openai_model = truncate_str(j.value("openai_model", "gpt-4o"), k_max_app_id);
		}
		cfg.workspace = truncate_str(j.value("workspace", cfg.workspace), k_max_path);
		if (!is_safe_path_component(cfg.workspace))
			throw std::runtime_error("invalid workspace path");
		cfg.storage_dir = truncate_str(j.value("storage_dir", cfg.storage_dir), k_max_path);
		if (!is_safe_path_component(cfg.storage_dir))
			throw std::runtime_error("invalid storage_dir path");
		if (auto admins = j.find("admins"); admins != j.end() && admins->is_array())
			for (auto const& a : *admins)
				if (a.is_string())
					cfg.admin_user_ids.push_back(truncate_str(a.get<std::string>(), k_max_app_id));
		cfg.default_mode = j.value("mode", "agent") == "agent" ? runtime_mode::agent : runtime_mode::plugin;

		registry.add_agent(cfg);
		registry.persist(config);
		return make_response(http::status::ok, json_response(http::status::ok, {{"id", cfg.id}}));
	}

	http::response<http::string_body> handle_agent_action(std::string const& id, std::string const& action) {
		if (action == "start")
			registry.start_agent(id);
		else if (action == "stop")
			registry.stop_agent(id);
		registry.persist(config);
		return make_response(http::status::ok, json_response(http::status::ok, {{"id", id}, {"action", action}}));
	}

	http::response<http::string_body> handle_agent_config(std::string const& id, std::string const& body) {
		auto j = nlohmann::json::parse(body);
		auto* inst = registry.get_agent(id);
		if (!inst)
			throw std::runtime_error("agent not found: " + id);

		auto cfg = inst->config;
		if (j.contains("name"))
			cfg.name = truncate_str(j["name"].get<std::string>(), k_max_agent_name);
		if (j.contains("enabled"))
			cfg.enabled = j["enabled"].get<bool>();
		if (j.contains("llm_provider")) {
			auto prov = truncate_str(j["llm_provider"].get<std::string>(), k_max_app_id);
			if (!is_valid_llm_provider(prov))
				throw std::runtime_error("invalid llm_provider: must be 'deepseek' or 'openai'");
			cfg.llm_provider = prov;
		}
		if (j.contains("workspace")) {
			auto ws = j["workspace"].get<std::string>();
			if (!is_safe_path_component(ws))
				throw std::runtime_error("invalid workspace path");
			cfg.workspace = truncate_str(std::move(ws), k_max_path);
		}
		if (j.contains("mode"))
			cfg.default_mode = j["mode"].get<std::string>() == "agent" ? runtime_mode::agent : runtime_mode::plugin;

		registry.update_agent_config(id, cfg);
		registry.persist(config);
		return make_response(http::status::ok, json_response(http::status::ok, {{"id", id}}));
	}

	http::response<http::string_body> handle_agent_remove(std::string const& id) {
		registry.remove_agent(id);
		registry.persist(config);
		return make_response(http::status::ok, json_response(http::status::ok, {{"id", id}}));
	}

	// ── log handler ──────────────────────────────────────────────────────
	// Query params: ?level=error&limit=50&since=2026-07-01T00:00:00

	http::response<http::string_body> handle_logs(std::string const& target) {
		// Simple query param parsing
		std::string level_filter;
		int limit = 100;
		std::string log_path = config.log_file;

		auto q_pos = target.find('?');
		if (q_pos != std::string::npos) {
			auto query = target.substr(q_pos + 1);
			// Parse key=value pairs
			size_t pos = 0;
			while (pos < query.size()) {
				auto eq = query.find('=', pos);
				auto amp = query.find('&', eq);
				if (eq == std::string::npos)
					break;
				auto key = query.substr(pos, eq - pos);
				auto val = query.substr(eq + 1, amp == std::string::npos ? std::string::npos : amp - eq - 1);
				if (key == "level")
					level_filter = val;
				else if (key == "limit")
					try { limit = std::stoi(val); } catch (...) {}
				pos = amp == std::string::npos ? query.size() : amp + 1;
			}
		}

		if (limit > 1000)
			limit = 1000;

		auto lines = read_log_lines(log_path, level_filter, limit);
		auto arr = nlohmann::json::array();
		for (auto const& line : lines)
			arr.push_back(line);
		return make_response(http::status::ok, json_response(http::status::ok, arr));
	}

	static std::vector<std::string> read_log_lines(std::string const& path, std::string const& filter, int limit) {
		std::vector<std::string> lines;
		if (path.empty())
			return lines;

		std::ifstream file(path);
		if (!file)
			return lines;

		std::string line;
		// Read from end-ish: we read all lines, filter, take last N
		std::vector<std::string> all;
		while (std::getline(file, line)) {
			if (filter.empty() || line.find("[" + filter + "]") != std::string::npos)
				all.push_back(line);
		}

		int start = std::max(0, static_cast<int>(all.size()) - limit);
		for (int i = start; i < static_cast<int>(all.size()); ++i)
			lines.push_back(std::move(all[i]));
		return lines;
	}

	// ── conversation handlers ────────────────────────────────────────────

	http::response<http::string_body> handle_conversations_list() {
		// Read conversation IDs from all agent storage dirs.
		// For now, return stub — full implementation needs cross-agent query.
		auto arr = nlohmann::json::array();
		auto agents = registry.list_agents();
		for (auto const& [id, status] : agents) {
			auto* inst = registry.get_agent(id);
			if (inst) {
				auto j = nlohmann::json::object();
				j["agent_id"] = id;
				j["agent_name"] = inst->config.name;
				j["message_count"] = inst->metrics.message_count.load();
				arr.push_back(std::move(j));
			}
		}
		return make_response(http::status::ok, json_response(http::status::ok, arr));
	}

	http::response<http::string_body> handle_conversation_detail(std::string const& /*convo_id*/) {
		// Stub: full conversation detail needs cross-agent SQLite query.
		return make_response(http::status::not_implemented,
							 error_response(http::status::not_implemented, "conversation detail not yet implemented"));
	}

	// ── metrics handler ────────────────────────────────────────────────

	http::response<http::string_body> handle_metrics() {
		auto j = nlohmann::json::object();
		j["status"] = "ok";

		// System snapshot
		auto sys = get_system_snapshot();
		auto sj = nlohmann::json::object();
		sj["cpu_percent"] = std::round(sys.cpu_percent * 10.0) / 10.0;
		sj["cpu_percent_recent"] = std::round(sys.cpu_percent_recent * 10.0) / 10.0;
		sj["memory_rss_mb"] = static_cast<int64_t>(sys.memory_rss_bytes / (1024 * 1024));
		sj["memory_virtual_mb"] = static_cast<int64_t>(sys.memory_virtual_bytes / (1024 * 1024));
		sj["thread_count"] = sys.thread_count;
		sj["uptime_seconds"] = sys.uptime_seconds;
		j["system"] = std::move(sj);

		// Worker stats
		size_t total_slots = 0;
		size_t total_depth = 0;
		auto agents = registry.list_agents();
		for (auto const& [id, st] : agents) {
			auto* inst = registry.get_agent(id);
			if (inst && inst->controller) {
				total_slots += inst->controller->workers().active_slot_count();
				total_depth += inst->controller->workers().total_queue_depth();
			}
		}
		auto wj = nlohmann::json::object();
		wj["total_slots"] = total_slots;
		wj["total_queue_depth"] = total_depth;
		j["workers"] = std::move(wj);

		// Per-agent metrics
		auto arr = nlohmann::json::array();
		int64_t agg_messages = 0, agg_tool_calls = 0, agg_prompt = 0, agg_completion = 0;
		for (auto const& [id, st] : agents) {
			auto* inst = registry.get_agent(id);
			if (!inst)
				continue;
			auto aj = nlohmann::json::object();
			aj["id"] = id;
			aj["status"] = std::string(to_string(st));
			auto& m = inst->metrics;
			auto mj = nlohmann::json::object();
			mj["message_count"] = m.message_count.load();
			mj["tool_call_count"] = m.tool_call_count.load();
			mj["prompt_tokens"] = m.prompt_tokens.load();
			mj["completion_tokens"] = m.completion_tokens.load();
			if (m.last_message_at.time_since_epoch().count() > 0) {
				auto tt = std::chrono::system_clock::to_time_t(m.last_message_at);
				std::ostringstream oss;
				oss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
				mj["last_message_at"] = oss.str();
			}
			if (m.started_at.time_since_epoch().count() > 0) {
				auto tt = std::chrono::system_clock::to_time_t(m.started_at);
				std::ostringstream oss;
				oss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
				mj["started_at"] = oss.str();
				mj["uptime_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(
										   std::chrono::system_clock::now() - m.started_at)
										   .count();
			}
			aj["metrics"] = std::move(mj);
			// Worker stats per agent
			if (inst->controller) {
				auto awj = nlohmann::json::object();
				awj["active_slots"] = inst->controller->workers().active_slot_count();
				awj["queue_depth"] = inst->controller->workers().total_queue_depth();
				aj["workers"] = std::move(awj);
			}
			arr.push_back(std::move(aj));
			agg_messages += m.message_count.load();
			agg_tool_calls += m.tool_call_count.load();
			agg_prompt += m.prompt_tokens.load();
			agg_completion += m.completion_tokens.load();
		}
		j["agents"] = std::move(arr);

		// Aggregate
		auto agg = nlohmann::json::object();
		agg["total_messages"] = agg_messages;
		agg["total_tool_calls"] = agg_tool_calls;
		agg["total_prompt_tokens"] = agg_prompt;
		agg["total_completion_tokens"] = agg_completion;
		j["aggregate"] = std::move(agg);

		return make_response(http::status::ok, json_response(http::status::ok, j));
	}

	// ── per-agent metrics ──────────────────────────────────────────────

	http::response<http::string_body> handle_agent_metrics(std::string const& id) {
		auto* inst = registry.get_agent(id);
		if (!inst)
			throw std::runtime_error("agent not found: " + id);

		auto j = nlohmann::json::object();
		j["id"] = id;
		j["status"] = std::string(to_string(inst->status));

		auto& m = inst->metrics;
		auto mj = nlohmann::json::object();
		mj["message_count"] = m.message_count.load();
		mj["tool_call_count"] = m.tool_call_count.load();
		mj["prompt_tokens"] = m.prompt_tokens.load();
		mj["completion_tokens"] = m.completion_tokens.load();
		if (m.last_message_at.time_since_epoch().count() > 0) {
			auto tt = std::chrono::system_clock::to_time_t(m.last_message_at);
			std::ostringstream oss;
			oss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
			mj["last_message_at"] = oss.str();
		}
		if (m.started_at.time_since_epoch().count() > 0) {
			auto tt = std::chrono::system_clock::to_time_t(m.started_at);
			std::ostringstream oss;
			oss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
			mj["started_at"] = oss.str();
		}
		j["metrics"] = std::move(mj);

		// Worker stats
		if (inst->controller) {
			auto wj = nlohmann::json::object();
			wj["active_slots"] = inst->controller->workers().active_slot_count();
			wj["queue_depth"] = inst->controller->workers().total_queue_depth();
			j["workers"] = std::move(wj);
		}
		if (!m.last_error.empty())
			j["last_error"] = m.last_error;

		return make_response(http::status::ok, json_response(http::status::ok, j));
	}

	// ── token stats handler ────────────────────────────────────────────

	http::response<http::string_body> handle_token_stats() {
		// Aggregate token stats across all agents
		// Map: date -> {requests, prompt, completion}
		std::map<std::string, std::tuple<int, int64_t, int64_t>> daily;
		auto agents = registry.list_agents();
		for (auto const& [id, st] : agents) {
			auto* inst = registry.get_agent(id);
			if (!inst || !inst->controller)
				continue;
			auto agent_stats = inst->controller->get_token_stats();
			for (auto const& entry : agent_stats) {
				auto& date   = std::get<0>(entry);
				auto requests = std::get<1>(entry);
				auto prompt   = std::get<2>(entry);
				auto completion = std::get<3>(entry);
				auto& [r, p, c] = daily[date];
				r += requests;
				p += prompt;
				c += completion;
			}
		}

		auto arr = nlohmann::json::array();
		for (auto const& [date, tup] : daily) {
			auto [requests, prompt, completion] = tup;
			auto entry = nlohmann::json::object();
			entry["date"] = date;
			entry["requests"] = requests;
			entry["prompt_tokens"] = prompt;
			entry["completion_tokens"] = completion;
			arr.push_back(std::move(entry));
		}
		return make_response(http::status::ok, json_response(http::status::ok, arr));
	}

	// ── helpers ──────────────────────────────────────────────────────────

	// Extract agent ID from paths like /api/v1/agents/<id>/start
	static std::string extract_id(std::string const& target, std::string const& suffix) {
		static constexpr std::string_view k_prefix = "/api/v1/agents/";
		auto id = target.substr(k_prefix.size(), target.size() - k_prefix.size() - suffix.size());
		if (!is_valid_agent_id(id))
			throw std::runtime_error("invalid agent id in URL: " + id);
		return std::string(id);
	}

	static std::string extract_id_raw(std::string const& target) {
		static constexpr std::string_view k_prefix = "/api/v1/agents/";
		if (target.starts_with(k_prefix)) {
			auto id = std::string(target.substr(k_prefix.size()));
			if (!is_valid_agent_id(id))
				throw std::runtime_error("invalid agent id in URL: " + id);
			return id;
		}
		return {};
	}

	static std::string extract_conversation_id(std::string const& target) {
		static constexpr std::string_view k_prefix = "/api/v1/conversations/";
		if (target.starts_with(k_prefix)) {
			auto id = std::string(target.substr(k_prefix.size()));
			if (!is_valid_conversation_id(id))
				throw std::runtime_error("invalid conversation id in URL: " + id);
			return id;
		}
		return {};
	}

	static http::response<http::string_body> make_response(http::status status, std::string body) {
		http::response<http::string_body> res{status, 11};
		res.set(http::field::content_type, "application/json");
		res.set(http::field::access_control_allow_origin, "http://localhost:3000");
		res.body() = std::move(body);
		res.prepare_payload();
		return res;
	}
};

// ─── management_api (pimpl) ─────────────────────────────────────────────────

management_api::management_api(agent_registry& registry, global_config const& global)
	: impl_(std::make_unique<impl>(registry, global)) {}

management_api::~management_api() { stop(); }

void management_api::start() {
	std::lock_guard<std::mutex> lk(impl_->lifecycle_mutex_);

	if (impl_->worker.joinable())
		return; // already started

	// Parse listen address (supports "host:port" and "[ipv6]:port").
	std::string host = "127.0.0.1";
	std::string port_str = "9090";
	auto const& listen_addr = impl_->config.management_listen;

	if (!listen_addr.empty()) {
		if (listen_addr[0] == '[') {
			// IPv6 bracketed format: [::1]:9090
			auto bracket = listen_addr.find(']');
			if (bracket != std::string::npos) {
				host = listen_addr.substr(1, bracket - 1);
				if (bracket + 1 < listen_addr.size() && listen_addr[bracket + 1] == ':')
					port_str = listen_addr.substr(bracket + 2);
			}
		} else {
			auto colon = listen_addr.rfind(':');
			if (colon != std::string::npos) {
				host = listen_addr.substr(0, colon);
				port_str = listen_addr.substr(colon + 1);
			} else {
				host = listen_addr;
			}
		}
	}

	unsigned short port = 9090;
	try {
		int p = std::stoi(port_str);
		if (p < 1 || p > 65535)
			throw std::runtime_error("port out of range");
		port = static_cast<unsigned short>(p);
	} catch (std::exception const& ex) {
		log::error(std::string("[mgmt-api] invalid port '") + port_str + "': " + ex.what());
		throw;
	}

	impl_->started_at = std::chrono::steady_clock::now();

	impl_->worker = std::thread([this, host, port]() {
		try {
			tcp::resolver resolver(impl_->ioc);
			auto const results = resolver.resolve(host, std::to_string(port));

			auto const endpoint = results.begin()->endpoint();
			impl_->acceptor.open(endpoint.protocol());
			impl_->acceptor.set_option(boost::asio::socket_base::reuse_address(true));
			impl_->acceptor.bind(endpoint);
			impl_->acceptor.listen(boost::asio::socket_base::max_listen_connections);

			log::info("[mgmt-api] listening on " + host + ":" + std::to_string(port));

			while (impl_->acceptor.is_open()) {
				tcp::socket socket(impl_->ioc);
				boost::system::error_code ec;
				impl_->acceptor.accept(socket, ec);
				if (ec) {
					if (ec == boost::asio::error::operation_aborted)
						break;
					log::warn("[mgmt-api] accept error: " + ec.message());
					continue;
				}

				// Read request
				beast::flat_buffer buffer;
				http::request_parser<http::string_body> parser;
				parser.body_limit(k_max_body);
				http::read(socket, buffer, parser, ec);
				auto req = parser.release();
				if (ec) {
					log::warn("[mgmt-api] read error: " + ec.message());
					// Try to send error response before continuing
					boost::system::error_code wec;
					http::response<http::string_body> err_res{http::status::bad_request, 11};
					err_res.set(http::field::content_type, "application/json");
					err_res.body() = R"({"status":"error","error":"bad request"})";
					err_res.prepare_payload();
					http::write(socket, err_res, wec);
					continue;
				}

				// Route and respond
				auto res = impl_->handle(std::move(req));
				http::write(socket, res, ec);

				// Close (no keep-alive for management API)
				socket.shutdown(tcp::socket::shutdown_send, ec);
			}
		} catch (std::exception const& ex) {
			log::error(std::string("[mgmt-api] fatal: ") + ex.what());
		}
	});
}

void management_api::stop() {
	std::lock_guard<std::mutex> lk(impl_->lifecycle_mutex_);

	if (!impl_->worker.joinable())
		return;

	boost::system::error_code ec;
	impl_->acceptor.close(ec);
	impl_->ioc.stop();
	if (impl_->worker.joinable())
		impl_->worker.join();
}

bool management_api::is_running() const { return impl_->worker.joinable(); }

} // namespace client
