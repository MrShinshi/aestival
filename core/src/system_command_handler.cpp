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
			std::time_t now_tt = std::time(nullptr);
			std::tm utc_tm;
			std::memcpy(&utc_tm, std::gmtime(&now_tt), sizeof(std::tm));
			int year  = utc_tm.tm_year + 1900;
			int month = utc_tm.tm_mon  + 1;

			try {
				auto raw = d.llm->query_usage_json(year, month);
				auto j = nlohmann::json::parse(raw, nullptr, false);
				if (!j.is_discarded() && !j.contains("error")) {
					auto const& amount = j["amount"];
					if (!amount.is_object()) {
						md << "_平台 API 返回数据格式异常（amount 非 object）_\n\n";
						d.reply_to(msg, md.str());
						return true;
					}
					auto const& days_arr = amount["days"];
					auto const& total_arr = amount["total"];
					if (days_arr.is_array() && !days_arr.empty()) {
						md << "### Token 用量\n\n"
						   << "| 日期 | 模型 | 请求 | Prompt | Completion | 缓存命中 | 缓存未命中 |\n"
						   << "|------|------|------|--------|------------|----------|------------|\n";

						// day_model: date_str -> model -> tuple(req, prompt, compl, hit, miss)
						std::map<std::string, std::map<std::string, std::tuple<int64_t, int64_t, int64_t, int64_t, int64_t>>>
							day_model;

						for (auto const& day : days_arr) {
							std::string date_str = day.value("date", "?");
							auto const& data_arr = day["data"];
							if (!data_arr.is_array()) continue;
							for (auto const& model_entry : data_arr) {
								std::string model = model_entry.value("model", "?");
								auto const& usage_arr = model_entry["usage"];
								if (!usage_arr.is_array()) continue;
								for (auto const& u : usage_arr) {
									std::string type = u.value("type", "");
									int64_t val = 0;
									try { val = std::stoll(u.value("amount", "0")); } catch (...) {}
									if (type == "REQUEST")
										std::get<0>(day_model[date_str][model]) += val;
									else if (type == "PROMPT_TOKEN")
										std::get<1>(day_model[date_str][model]) += val;
									else if (type == "RESPONSE_TOKEN")
										std::get<2>(day_model[date_str][model]) += val;
									else if (type == "PROMPT_CACHE_HIT_TOKEN")
										std::get<3>(day_model[date_str][model]) += val;
									else if (type == "PROMPT_CACHE_MISS_TOKEN")
										std::get<4>(day_model[date_str][model]) += val;
								}
							}
						}

						int64_t tr = 0, tp = 0, tc = 0, th = 0, tm = 0;
						for (auto const& [date_str, model_map] : day_model) {
							for (auto const& [model, tup] : model_map) {
								auto [req, prompt, cmpl, hit, miss] = tup;
								md << "| " << date_str << " | `" << model << "`"
								   << " | " << req
								   << " | " << (prompt / 1000) << "K"
								   << " | " << (cmpl / 1000) << "K"
								   << " | " << (hit / 1000) << "K"
								   << " | " << (miss / 1000) << "K |\n";
								tr += req; tp += prompt; tc += cmpl; th += hit; tm += miss;
							}
						}

						md << "\n**合计**: " << tr << " 请求, " << (tp / 1000) << "K prompt, "
						   << (tc / 1000) << "K compl, " << (th / 1000) << "K 缓存命中, "
						   << (tm / 1000) << "K 缓存未命中\n";

						// model list from total
						if (total_arr.is_array() && !total_arr.empty()) {
							md << "\n模型: ";
							bool first = true;
							for (auto const& m : total_arr) {
								if (!first) md << ", ";
								first = false;
								md << "`" << m.value("model", "?") << "`";
							}
							md << "\n";
						}
					}

					auto const& cost_arr = j["cost"];
					if (cost_arr.is_array() && !cost_arr.empty()) {
						md << "### 费用\n\n"
						   << "| 日期 | 模型 | 费用 (CNY) |\n"
						   << "|------|------|-----------|\n";

						double grand_total = 0;
						for (auto const& currency_group : cost_arr) {
							auto const& cost_days = currency_group["days"];
							if (!cost_days.is_array()) continue;
							for (auto const& day : cost_days) {
								std::string date_str = day.value("date", "?");
								auto const& data_arr = day["data"];
								if (!data_arr.is_array()) continue;
								for (auto const& model_entry : data_arr) {
									std::string model = model_entry.value("model", "?");
									double day_cost = 0;
									auto const& usage_arr = model_entry["usage"];
									if (!usage_arr.is_array()) continue;
									for (auto const& u : usage_arr) {
										double v = 0;
										try { v = std::stod(u.value("amount", "0")); } catch (...) {}
										day_cost += v;
									}
									md << "| " << date_str << " | `" << model << "`"
									   << " | " << day_cost << " |\n";
									grand_total += day_cost;
								}
							}
						}
						md << "\n**合计**: " << grand_total << " CNY\n";
					}

					if (!days_arr.is_array() || days_arr.empty())
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
