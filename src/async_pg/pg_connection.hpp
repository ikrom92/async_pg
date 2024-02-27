#pragma once

#include <map>
#include <string>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <list>
#include <unordered_set>

#include "pg_result.hpp"
#include "pg_query.hpp"
#include "libpq-fe.h"

class pg_connection {
public:
	pg_connection(const pg_connection&) = delete;
	pg_connection& operator=(const pg_connection&) = delete;
	pg_connection(pg_connection&&) = delete;
	pg_connection& operator=(pg_connection&&) = delete;

	pg_connection();
	~pg_connection();

	bool start_connect(const std::map<std::string, std::string>& params);
	bool start_reset();
	
	bool start_send_query(const std::string& sql, const std::list<pg_param>& params = {});
	bool start_send_prepared_query(const std::string& name, const std::list<pg_param>& params = {});
	bool start_send_prepared_statement(const std::string& name, const std::string& sql);
	bool has_prepared_statement(const std::string& name);

	bool get_results(std::list<pg_result>& results);
	bool get_notifies();

	int socket();
	bool read();
	bool write();

	bool poll_read();
	bool poll_write();
	PostgresPollingStatusType connectPoll();
	PostgresPollingStatusType resetPoll();
	
	enum class async_state_t {
		connecting,
		resetting,
		connection_failed,
		connection_abort,
		executing_query,
		idle,
	};

	const async_state_t& async_state() { return _async_state; }
	const std::string& last_error() const { return _last_error; }

private:
	PGconn* _conn;
	std::string _last_error;
	bool _need_flush;
	async_state_t _async_state;
	std::unordered_set<std::string> _prepared_statements;
};