/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "management_api.h"
#include "agent_instance.h"
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

static std::string method_str(http::verb v) {
	switch (v) {
	case http::verb::get: return "GET";
	case http::verb::post: return "POST";
	case http::verb::put: return "PUT";
	case http::verb::delete_: return "DELETE";
	default: return "UNKNOWN";
	}
}

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

static std::string error_response(http::status status, std::string_view msg) {
	auto j = nlohmann::json::object({{"status", "error"}, {"error", std::string(msg)}});
	return j.dump();
}

// ─── impl ───────────────────────────────────────────────────────────────────

struct management_api::impl {
	agent_registry& registry;
	global_config const& config;
	mgmt::jwt_verifier jwt;
	std::chrono::steady_clock::time_point started_at;

	boost::asio::io_context ioc;
	tcp::acceptor acceptor;
	std::thread worker;

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
			if (target.starts_with("/api/v1/agents/") && method == http::verb::delete_)
				return handle_agent_remove(extract_id_raw(target));

			// ── Log routes ───────────────────────────────────────────────
			if (target.starts_with("/api/v1/logs") && method == http::verb::get)
				return handle_logs(target);

			// ── Conversation routes ──────────────────────────────────────
			if (target == "/api/v1/conversations" && method == http::verb::get)
				return handle_conversations_list();
			if (target.starts_with("/api/v1/conversations/") && method == http::verb::get)
				return handle_conversation_detail(extract_id_raw(target));

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

		cfg.id = j.value("id", "");
		cfg.name = j.value("name", "");
		cfg.platform = j.value("platform", "qq");
		cfg.enabled = j.value("enabled", true);

		if (auto qq = j.find("qq"); qq != j.end()) {
			cfg.qq_app_id = qq->value("app_id", "");
			cfg.qq_app_secret = qq->value("app_secret", "");
		}
		cfg.llm_provider = j.value("llm_provider", "deepseek");
		if (auto ds = j.find("deepseek"); ds != j.end()) {
			cfg.deepseek_api_key = ds->value("api_key", "");
			cfg.deepseek_model = ds->value("model", "deepseek-chat");
		}
		cfg.workspace = j.value("workspace", cfg.workspace);
		cfg.storage_dir = j.value("storage_dir", cfg.storage_dir);
		if (auto admins = j.find("admins"); admins != j.end() && admins->is_array())
			for (auto const& a : *admins)
				if (a.is_string())
					cfg.admin_user_ids.push_back(a.get<std::string>());
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
			cfg.name = j["name"].get<std::string>();
		if (j.contains("enabled"))
			cfg.enabled = j["enabled"].get<bool>();
		if (j.contains("llm_provider"))
			cfg.llm_provider = j["llm_provider"].get<std::string>();
		if (j.contains("workspace"))
			cfg.workspace = j["workspace"].get<std::string>();
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
		return make_response(http::status::not_found,
							 error_response(http::status::not_found, "conversation detail not yet implemented"));
	}

	// ── helpers ──────────────────────────────────────────────────────────

	// Extract agent ID from paths like /api/v1/agents/<id>/start
	static std::string extract_id(std::string const& target, std::string const& suffix) {
		static constexpr std::string_view k_prefix = "/api/v1/agents/";
		auto id = target.substr(k_prefix.size(), target.size() - k_prefix.size() - suffix.size());
		return std::string(id);
	}

	static std::string extract_id_raw(std::string const& target) {
		static constexpr std::string_view k_prefix = "/api/v1/agents/";
		static constexpr std::string_view k_conv_prefix = "/api/v1/conversations/";
		if (target.starts_with(k_prefix))
			return std::string(target.substr(k_prefix.size()));
		if (target.starts_with(k_conv_prefix))
			return std::string(target.substr(k_conv_prefix.size()));
		return {};
	}

	static http::response<http::string_body> make_response(http::status status, std::string body) {
		http::response<http::string_body> res{status, 11};
		res.set(http::field::content_type, "application/json");
		res.set(http::field::access_control_allow_origin, "*");
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
	if (impl_->worker.joinable())
		return; // already started

	// Parse listen address
	auto colon = impl_->config.management_listen.find(':');
	std::string host = impl_->config.management_listen.substr(0, colon);
	std::string port_str = colon != std::string::npos ? impl_->config.management_listen.substr(colon + 1) : "9090";
	auto port = static_cast<unsigned short>(std::stoi(port_str));

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
				http::request<http::string_body> req;
				http::read(socket, buffer, req, ec);
				if (ec) {
					log::warn("[mgmt-api] read error: " + ec.message());
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
