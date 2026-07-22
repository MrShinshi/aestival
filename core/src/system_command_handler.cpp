/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#include "stdafx.h"
#include "system_command_handler.h"
#include "model_client.h"
#include "encode_utils.h"
#include "log.h"

#include <iomanip>
#include <ctime>

namespace client {

static constexpr int k_switch_mode_prefix_len = 12; // "switch mode ".size()

bool system_command_handler::is_admin(message_event const& msg, std::unordered_set<std::string> const& admin_ids) {
	std::string id = msg.is_private && !msg.is_guild ? msg.user_openid : msg.sender_id;
	if (!id.empty() && admin_ids.count(id))
		return true;
	if (msg.is_guild && msg.is_private && !msg.user_openid.empty() && admin_ids.count(msg.user_openid))
		return true;
	return false;
}

bool system_command_handler::handle(std::string const& n, message_event const& msg, system_command_deps const& d) {
	if (n == "switch mode") {
		runtime_mode m;
		{
			std::lock_guard<std::mutex> lk(d.mode_mutex);
			m = d.mode;
		}
		d.reply_to(msg, "Current mode: " + std::string(to_string(m)));
		return true;
	}
	if (client::starts_with(n, "switch mode ")) {
		if (!is_admin(msg, d.admin_ids)) {
			d.reply_to(msg, "Permission denied.");
			return true;
		}
		std::string v = client::trim(n.substr(k_switch_mode_prefix_len));
		runtime_mode next = (v == "agent") ? runtime_mode::agent : runtime_mode::plugin;
		if (v != "agent" && v != "plugin") {
			d.reply_to(msg, "Usage: switch mode plugin|agent");
			return true;
		}
		{
			std::lock_guard<std::mutex> lk(d.mode_mutex);
			d.mode = next;
		}
		d.reply_to(msg, "Mode -> " + std::string(to_string(next)));
		return true;
	}

	if (n == "clear") {
		d.contexts.clear(d.actor_id_of(msg));
		d.reply_to(msg, "ok");
		return true;
	}

	if (n == "usage") {
		std::ostringstream md;
		md << "## DeepSeek 用量\n\n";

		if (d.llm && d.llm->provider_name() == "deepseek") {
			int64_t end_sec =
				std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
					.count();
			int64_t start_sec = end_sec - 30LL * 86400LL;

			try {
				auto raw = d.llm->query_usage_json(start_sec, end_sec);
				auto j = nlohmann::json::parse(raw, nullptr, false);
				if (!j.is_discarded() && !j.contains("error")) {
					auto const& amount = j["amount"];
					if (!amount.is_object()) {
						md << "_平台 API 返回数据格式异常（amount 非 object）_\n\n";
						d.reply_to(msg, md.str());
						return true;
					}
					auto const& series = amount["series"];
					auto const& models = amount["models"];
					if (series.is_array() && !series.empty()) {
						md << "### Token 用量\n\n"
						   << "| 日期 | 模型 | 请求 | 输出 | 缓存命中 | 缓存未命中 |\n"
						   << "|------|------|------|------|----------|------------|\n";

						std::map<int64_t, std::map<std::string, std::tuple<int64_t, int64_t, int64_t, int64_t>>>
							day_model;

						for (auto const& s : series) {
							std::string model = s.value("model", "?");
							auto const& buckets = s["buckets"];
							if (!buckets.is_array())
								continue;
							for (auto const& b : buckets) {
								int64_t t = b.value("time", 0LL);
								auto const& u = b["usage"];
								if (!u.is_object())
									continue;
								std::get<0>(day_model[t][model]) += u.value("REQUEST", 0);
								std::get<1>(day_model[t][model]) += u.value("RESPONSE_TOKEN", 0);
								std::get<2>(day_model[t][model]) += u.value("PROMPT_CACHE_HIT_TOKEN", 0);
								std::get<3>(day_model[t][model]) += u.value("PROMPT_CACHE_MISS_TOKEN", 0);
							}
						}

						int64_t tr = 0, to = 0, th = 0, tm = 0;
						for (auto const& [date, model_map] : day_model) {
							std::time_t tt = static_cast<std::time_t>(date);
							char dbuf[16];
							std::strftime(dbuf, sizeof(dbuf), "%m-%d", std::gmtime(&tt));
							for (auto const& [model, tup] : model_map) {
								auto [req, out, hit, miss] = tup;
								md << "| " << dbuf << " | `" << model << "`"
								   << " | " << req << " | " << (out / 1000) << "K"
								   << " | " << (hit / 1000) << "K"
								   << " | " << (miss / 1000) << "K |\n";
								tr += req;
								to += out;
								th += hit;
								tm += miss;
							}
						}

						md << "\n**合计**: " << tr << " 请求, " << (to / 1000) << "K 输出, " << (th / 1000)
						   << "K 缓存命中, " << (tm / 1000) << "K 缓存未命中\n";

						if (models.is_array() && !models.empty()) {
							md << "\n模型: ";
							for (size_t i = 0; i < models.size(); ++i) {
								if (i)
									md << ", ";
								md << "`" << (models[i].is_string() ? models[i].get<std::string>() : "unknown") << "`";
							}
							md << "\n";
						}
					}

					auto const& cost_obj = j["cost"];
					if (cost_obj.is_object()) {
						auto const& cost_data = cost_obj["data"];
						if (cost_data.is_array() && !cost_data.empty()) {
							md << "### 费用\n\n"
							   << "| 日期 | 模型 | 币种 | 费用 |\n"
							   << "|------|------|------|------|\n";

							for (auto const& cg : cost_data) {
								auto const& cs = cg["series"];
								if (!cs.is_array() || cs.empty())
									continue;
								std::string cur = cg.value("currency", "?");

								double total_cost = 0;
								for (auto const& s : cs) {
									std::string model = s.value("model", "?");
									auto const& cb = s["buckets"];
									if (!cb.is_array())
										continue;
									for (auto const& b : cb) {
										double cv = 0;
										try {
											cv = std::stod(b.value("cost", "0"));
										} catch (...) {
										}
										int64_t t = b.value("time", 0LL);
										std::time_t tt = static_cast<std::time_t>(t);
										char dbuf[16];
										std::strftime(dbuf, sizeof(dbuf), "%m-%d", std::gmtime(&tt));
										md << "| " << dbuf << " | `" << model << "`"
										   << " | " << cur << " | " << cv << " |\n";
										total_cost += cv;
									}
								}
								md << "\n**合计**: " << total_cost << " " << cur << "\n";
							}
						}
					}

					if (!series.is_array() || series.empty())
						md << "_平台 API 返回空数据_\n";
				} else {
					md << "_平台 API 不可用: " << j.value("error", "unknown") << "_\n\n";
				}
			} catch (std::exception const& ex) {
				md << "_平台 API 异常: " << ex.what() << "_\n\n";
			}
		} else {
			md << "_未配置 DeepSeek 或用的是其他 LLM 后端_\n\n";
		}

		auto stats = d.contexts.get_token_stats();
		if (!stats.empty()) {
			md << "### 本地统计\n\n"
			   << "| 日期 | 请求 | Prompt | Completion | 总计 |\n"
			   << "|------|------|--------|------------|------|\n";
			int64_t gp = 0, gc = 0, gr = 0;
			for (auto const& [date, count, p, c] : stats) {
				md << "| " << date << " | " << count << " | " << p << " | " << c << " | " << (p + c) << " |\n";
				gr += count;
				gp += p;
				gc += c;
			}
			md << "\n**30天合计**: " << gr << " 请求, " << gp << " prompt, " << gc << " compl, " << (gp + gc)
			   << " total\n";
		}
		d.reply_to(msg, md.str());
		return true;
	}

	if (n == "delete database") {
		if (!is_admin(msg, d.admin_ids)) {
			d.reply_to(msg, "Permission denied.");
			return true;
		}
		std::string db_path = d.storage_dir + "/conversations.db";
		if (std::filesystem::exists(db_path)) {
			std::filesystem::remove(db_path);
			d.reply_to(msg, "数据库已删除，请重启 Bot 以重建。");
		} else {
			d.reply_to(msg, "数据库文件不存在。");
		}
		return true;
	}

	if (n == "stop") {
		if (!is_admin(msg, d.admin_ids)) {
			d.reply_to(msg, "Permission denied.");
			return true;
		}
		d.reply_to(msg, "Shutting down...");
		if (d.on_stop)
			d.on_stop();
		return true;
	}

	if (n == "self-iterate" || n == "self-iterate dry-run") {
		if (!is_admin(msg, d.admin_ids)) {
			d.reply_to(msg, "Permission denied.");
			return true;
		}
		bool dry = (n == "self-iterate dry-run");
		if (d.on_self_iterate) {
			std::string label = dry ? "评估(dry-run)" : "";
			d.reply_to(msg, "自迭代" + label + "运行中，请稍候...");
			std::string result = d.on_self_iterate(dry);
			d.reply_to(msg, result);
		} else {
			d.reply_to(msg, "自迭代模块未配置。");
		}
		return true;
	}

	return false;
}

} // namespace client
