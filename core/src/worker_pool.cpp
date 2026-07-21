/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "worker_pool.h"
#include "log.h"

namespace client {

worker_pool::~worker_pool() {
	stop();
}

void worker_pool::start(processor_fn proc, key_fn key) {
	proc_ = std::move(proc);
	key_ = std::move(key);
}

void worker_pool::stop() {
	stopping_ = true;
	std::lock_guard<std::mutex> lock(map_mutex_);
	for (auto& [id, s] : slots_) {
		{
			std::lock_guard<std::mutex> lk(s->mtx);
			s->stopping = true;
		}
		s->cv.notify_all();
	}
	for (auto& [id, s] : slots_)
		if (s->worker.joinable())
			s->worker.join();
	slots_.clear();
}

void worker_pool::enqueue(message_event const& msg) {
	std::string cid = key_(msg);
	if (cid.empty())
		cid = "global";

	slot* s = nullptr;
	{
		std::lock_guard<std::mutex> lock(map_mutex_);
		auto it = slots_.find(cid);
		if (it == slots_.end()) {
			auto sl = std::make_unique<slot>();
			s = sl.get();
			slots_[cid] = std::move(sl);
			s->worker = std::thread([this, sp = s, id = cid]() { worker_loop(*sp, id); });
		} else {
			s = it->second.get();
		}
	}
	{
		std::lock_guard<std::mutex> lock(s->mtx);
		s->queue.push(msg);
	}
	s->cv.notify_one();
}

void worker_pool::worker_loop(slot& s, std::string cid) {
	log::info("[worker] started: " + cid);
	for (;;) {
		message_event msg;
		{
			std::unique_lock<std::mutex> lock(s.mtx);
			s.cv.wait(lock, [&] { return s.stopping || stopping_ || !s.queue.empty(); });
			if ((s.stopping || stopping_) && s.queue.empty())
				break;
			msg = std::move(s.queue.front());
			s.queue.pop();
		}
		if (proc_)
			proc_(msg);
	}
	log::info("[worker] stopped: " + cid);
}

} // namespace client
