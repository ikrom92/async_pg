#pragma once

#include <map>
#include <string>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <list>

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
	bool start_send(pg_query&& query);
	
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
		idle,
		executing_query,
	};

	const async_state_t& async_state() { return _async_state; }
	const std::string& last_error() const { return _last_error; }

private:
	std::map<std::string, std::string> _connection_params;
	pg_query _query;
	PGconn* _conn;
	std::string _last_error;
	bool _has_query;
	bool _need_flush;
	async_state_t _async_state;
};