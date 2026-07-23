/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "bot_config.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

namespace boost::asio { class io_context; }
namespace platform::qq { struct session; }

namespace client {

struct agent_controller;
struct model_client;
struct bot_messaging;
struct plugin_manager;
struct agent_reach_client;
struct chat_storage_backend;

// ─── agent_status ──────────────────────────────────────────────────────────

enum struct agent_status : int {
	stopped,
	starting,
	running,
	stopping,
	error
};

inline std::string_view to_string(agent_status s) {
	switch (s) {
	case agent_status::stopped: return "stopped";
	case agent_status::starting: return "starting";
	case agent_status::running: return "running";
	case agent_status::stopping: return "stopping";
	case agent_status::error: return "error";
	}
	return "unknown";
}

// ─── agent_metrics ─────────────────────────────────────────────────────────

struct agent_metrics {
	std::chrono::system_clock::time_point started_at{};
	std::chrono::system_clock::time_point last_message_at{};
	std::atomic<int64_t> message_count{0};
	std::atomic<int64_t> tool_call_count{0};
	std::atomic<int64_t> prompt_tokens{0};
	std::atomic<int64_t> completion_tokens{0};
	std::string last_error;
};

// ─── agent_instance ────────────────────────────────────────────────────────
//
// Owns the full lifecycle of one agent: platform session, model client,
// IM adapter, agent controller, and metrics.
//
// Created by agent_registry and never copied or moved once running.

struct agent_instance {
	agent_config config;
	std::unique_ptr<platform::qq::session> session; // concrete QQ (TODO: abstract interface for wechat)
	std::unique_ptr<bot_messaging> im;		// adapter (qq_adapter / console_api)
	std::unique_ptr<model_client> llm;		// deepseek or openai
	std::unique_ptr<agent_controller> controller;

	agent_status status = agent_status::stopped;
	agent_metrics metrics;

	mutable std::mutex mutex; // protects status + metrics.last_error

	// Lifecycle flag: shared_ptr held by the instance, weak_ptr captured by
	// the delayed notification thread in launch_agent().  When the instance
	// is stopped/destroyed, this flag is signaled before controller.reset()
	// so the thread can observe the cancellation and skip notify_startup().
	std::shared_ptr<std::atomic<bool>> alive_flag;

	agent_instance() = default;
	agent_instance(agent_instance const&) = delete;
	agent_instance& operator=(agent_instance const&) = delete;
	agent_instance(agent_instance&&) noexcept = default;
	agent_instance& operator=(agent_instance&&) noexcept = default;
};

} // namespace client
