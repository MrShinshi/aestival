/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once

#include "message_types.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

namespace client {

// ─── worker_pool ──────────────────────────────────────────────────────────
// Extracted from agent_controller: per-conversation serial worker threads.
//
// Each conversation gets its own thread + queue.  Messages for the same
// conversation are processed sequentially; different conversations run
// in parallel.
//
// The pool takes a `processor` callback that does the actual work
// (process_agent_message).  Thread-safety: the callback is serialized
// per conversation by design — no two calls for the same convo_id
// overlap.

struct worker_pool {
	using processor_fn = std::function<void(message_event const&)>;

	// actor_id_of: message → convo_id (supplied by controller)
	using key_fn = std::function<std::string(message_event const&)>;

	worker_pool() = default;
	~worker_pool();

	// Start processing.  `proc` is called once per message, serialized
	// per conversation.  `key` extracts the convo_id from a message.
	void start(processor_fn proc, key_fn key);

	// Enqueue a message.  Returns immediately; processing is async.
	void enqueue(message_event const& msg);

	// Signal shutdown and join all worker threads.
	void stop();

	private:
	struct slot {
		std::mutex mtx;
		std::queue<message_event> queue;
		std::condition_variable cv;
		std::thread worker;
		bool stopping = false;
	};

	void worker_loop(slot& s, std::string convo_id);

	processor_fn proc_;
	key_fn key_;

	std::atomic<bool> stopping_{false};
	std::mutex map_mutex_;
	std::unordered_map<std::string, std::unique_ptr<slot>> slots_;
};

} // namespace client
