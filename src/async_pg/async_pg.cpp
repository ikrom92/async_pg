#include "async_pg.hpp"
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <errno.h>
#include <unordered_map>
#include <string.h>
#include "pg_connection.hpp"

#define log_error(...) printf(__VA_ARGS__); printf("\n")
#define log_info(...) printf(__VA_ARGS__); printf("\n")


// Rules:
// 1. Don't call PQconsumeInput on PGRES_POLLING_READING.
//		If you call PQconsumeInput on PGRES_POLLING_READING, 
//		you will stuck in receiving PGRES_POLLING_READING for PQconnectStatus
// 2. You shouldn't call PQflush on PGRES_POLLING_WRITING as well.
//		Of course, there were no problem when I call PQflush, but, 
//		to respect 1st rule don't call PQflush.
// 3. After PQconnectStart, you MUST always call PQconnectStatus, 
//		until it returns PGRES_POLLING_OK or PGRES_POLLING_FAILED.
//		First time call PQconnectStatus, it returns PGRES_POLLING_WRITING.
//		You need to wait until socket will be writable.
//		On next call, PQconnectStatus returns PGRES_POLLING_READING.
//		It doesn't matter how much time passed between PQconnectStart and PQconnectStatus.
//		Even if I put sleep between calls to PQconnectStatus, the return value order is the same
//
// 4. You cannot know if connection failed after success connect. 
//		You can know it only when you try to send some command to that connection.
//		Otherwise, you will always get PGRES_POLLING_OK.
// 5. Call PQgetResult until it returns nullptr. Even if you that only one result will return, 
//		call it until it returns nullptr. If you don't do that, next PQsendQuery call returns FALSE.
// 6. If PQisBusy returns TRUE, wait for read- and write- ready

async_pg::async_pg(std::string connection_string) :
	async_pg({ {"dbname", connection_string} }) {}

async_pg::async_pg(std::string ip, std::string port, std::string dbname, std::string user, std::string password, std::string ssl_mode) :
	async_pg({
		{"hostaddr", ip},
		{"port", port},
		{"dbname", dbname},
		{"user", user},
		{"password", password},
		{"sslmode", ssl_mode}}) {}

async_pg::async_pg(std::map<std::string, std::string> params) {
	_connection_params = params;
	_running = false;
	_notifiy_fd = -1;
	_wait_fd = -1;

	int pipes[2];
	if (pipe(pipes) == -1) {
		std::string error = strerror(errno);
		throw std::runtime_error("pipe() failed: " + error);
	}
	_wait_fd = pipes[0];
	_notifiy_fd = pipes[1];
}

async_pg::~async_pg() {
	if (_running) {
		stop();
	}

	if (_wait_fd != -1) {
		close(_wait_fd);
	}

	if (_notifiy_fd != -1) {
		close(_notifiy_fd);
	}
}

void async_pg::start(int n_connections) {

	std::lock_guard<std::mutex> lock(_mtx);
	if (!_running) {
		_running = true;
		_thr = std::thread(&async_pg::process, this, n_connections);
	}

}

void async_pg::stop() {

	std::unique_lock<std::mutex> lock(_mtx);
	if (_running) {
		_running = false;
		lock.unlock();
		cond_notify();
		if (_thr.joinable()) {
			_thr.join();
		}
	}

}

std::future<std::list<pg_result>> async_pg::execute(std::string&& sql, std::list<pg_param>&& params) {

	pg_query query(std::move(sql), std::move(params));
	auto future = query.get_future();

	std::lock_guard<std::mutex> lock(_mtx);
	_queries.push_back(std::move(query));
	cond_notify();
	return future;
}

std::future<std::list<pg_result>> async_pg::execute(const std::string& sql, const std::list<pg_param>& params) {

	pg_query query(sql, params);
	auto future = query.get_future();

	std::lock_guard<std::mutex> lock(_mtx);
	_queries.push_back(std::move(query));
	cond_notify();
	return future;
}

std::future<std::list<pg_result>> async_pg::execute_prepared(std::string&& name, std::string&& sql, std::list<pg_param>&& params) {

	pg_query query(std::move(name), std::move(sql), std::move(params));
	auto future = query.get_future();

	std::lock_guard<std::mutex> lock(_mtx);
	_queries.push_back(std::move(query));
	cond_notify();
	return future;
}

std::future<std::list<pg_result>> async_pg::execute_prepared(const std::string& name, const std::string& sql, const std::list<pg_param>& params) {

	pg_query query(name, sql, params);
	auto future = query.get_future();

	std::lock_guard<std::mutex> lock(_mtx);
	_queries.push_back(std::move(query));
	cond_notify();
	return future;
}

void async_pg::process(int n_connections) {
	
	std::vector<std::shared_ptr<pg_connection>> connections;
	for (int i = 0; i < n_connections; ++i) {
		auto conn = std::make_shared<pg_connection>(i + 1);
		if (conn->start_connect(_connection_params)) {
			connections.push_back(conn);
		}
		else {
			log_error("\t[%02d] start_connect -> %s", conn->id(), conn->last_error().c_str());
		}
	}
	
	if (connections.empty()) {
		log_error("\tasync_pg service failed. no success connections");
		return;
	}

	int efd = epoll_create1(0);

	{
		epoll_event e;
		e.events = EPOLLIN;
		e.data.fd = _wait_fd;
		if (epoll_ctl(efd, EPOLL_CTL_ADD, _wait_fd, &e) == -1) {
			log_error("failed to add _wait_fd to epoll");
			return;
		}
	}
		
	epoll_event* events = new epoll_event[n_connections];
	std::unordered_map<int, std::shared_ptr<pg_connection>> scheduled_connections;
	std::list<pg_query> queries;
	std::unordered_map<int, pg_query> scheduled_queries;

	while (true) {

		{
			std::lock_guard<std::mutex> lock(_mtx);
			if (!_running) {
				break;
			}
		}

		// schedule events
		for (auto conn: connections) {
			epoll_event event;
			event.events = EPOLLERR;

			// conn->connectPoll() might change async_state of connection
			if (conn->async_state() == pg_connection::async_state_t::connecting) {
				log_info("[%02d] async_state_t::connecting", conn->id());
				PostgresPollingStatusType s = conn->connectPoll();
				if (s == PostgresPollingStatusType::PGRES_POLLING_READING) {
					event.events |= EPOLLIN;
				}
				else if (s == PostgresPollingStatusType::PGRES_POLLING_WRITING) {
					event.events |= EPOLLOUT;
				}
			}

			// conn->resetPoll() might change async_state of connection
			if (conn->async_state() == pg_connection::async_state_t::resetting) {
				log_info("[%02d] async_state_t::resetting", conn->id());
				PostgresPollingStatusType s = conn->resetPoll();
				if (s == PostgresPollingStatusType::PGRES_POLLING_READING) {
					event.events |= EPOLLIN;
				}
				else if (s == PostgresPollingStatusType::PGRES_POLLING_WRITING) {
					event.events |= EPOLLOUT;
				}
			}

			if (conn->async_state() == pg_connection::async_state_t::connection_failed) {
				log_info("[%02d] async_state_t::connection_failed: %s", conn->id(), conn->last_error().c_str());
				if (!conn->start_connect(_connection_params)) {
					log_error("\t[%02d] start_connect -> %s", conn->id(), conn->last_error().c_str());
				}
			}

			if (conn->async_state() == pg_connection::async_state_t::connection_abort) {
				log_info("[%02d] async_state_t::connection_abort: %s", conn->id(), conn->last_error().c_str());
				if (!conn->start_reset()) {
					log_error("\t[%02d] start_reset -> %s", conn->id(), conn->last_error().c_str());
				}
			}

			if (conn->async_state() == pg_connection::async_state_t::executing_query) {
				
				if (conn->poll_read()) {
					event.events |= EPOLLIN;
				}

				if (conn->poll_write()) {
					event.events |= EPOLLOUT;
				}

			}

			if (conn->async_state() == pg_connection::async_state_t::idle) {

				if (queries.size()) {
					if (queries.front().name().empty()) {
						if (conn->start_send_query(queries.front().sql(), queries.front().params())) {
							scheduled_queries[conn->id()] = std::move(queries.front());
							queries.pop_front();
						}
						else {
							log_error("[%02d] start_send_query -> %s", conn->id(), conn->last_error().c_str());
						}
					}
					else if (conn->has_prepared_statement(queries.front().name())) {
						if (conn->start_send_prepared_query(queries.front().name(), queries.front().params())) {
							scheduled_queries[conn->id()] = std::move(queries.front());
							queries.pop_front();
						}
						else {
							log_error("[%02d] start_send_prepared_query -> %s", conn->id(), conn->last_error().c_str());
						}
					}
					else {
						if (!conn->start_send_prepared_statement(queries.front().name(), queries.front().sql())) {
							log_error("[%02d] start_send_prepared_statement -> %s", conn->id(), conn->last_error().c_str());
						}
					}
				}

				if (conn->poll_read()) {
					event.events |= EPOLLIN;
				}

				if (conn->poll_write()) {
					event.events |= EPOLLOUT;
				}
			}

			if (event.events & EPOLLIN || event.events & EPOLLOUT) {
				int sock = conn->socket();
				event.data.fd = sock;
				if (epoll_ctl(efd, EPOLL_CTL_ADD, sock, &event) == 0) {
					scheduled_connections[sock] = conn;
				}
			}
			
		}

		// wait for events
		int n_events = epoll_wait(efd, events, n_connections, 400);
		if (n_events == -1) {
			log_error("epoll_wait -> %d", errno);
		}

		// read events
		for (int i = 0; i < n_events; ++i) {
			epoll_event event = events[i];
			if (scheduled_connections.count(event.data.fd)) {
				auto conn = scheduled_connections.at(event.data.fd);
				if (conn->async_state() == pg_connection::async_state_t::executing_query) {

					if (event.events & EPOLLIN) {
						conn->read();
					}
					if (event.events & EPOLLOUT) {
						conn->write();
					}

					std::list<pg_result> results;
					if (conn->get_results(results)) {
						if (scheduled_queries.count(conn->id())) {
							scheduled_queries.at(conn->id()).set_result(std::move(results));
							scheduled_queries.erase(conn->id());
						}
					}
				}
				
				if (event.events & EPOLLERR) {
					log_error("\t[%02d] EPOLLERR", conn->id());
				}

				epoll_ctl(efd, EPOLL_CTL_DEL, event.data.fd, nullptr);
				scheduled_connections.erase(event.data.fd);
			}
			else if (event.data.fd == _wait_fd) {
				cond_reset();
			}

		}

		// get new requests
		std::lock_guard<std::mutex> lock(_mtx);
		if (_queries.size() > 0) {
			for (pg_query& q : _queries) {
				queries.push_back(std::move(q));
			}
			_queries.clear();
		}
	}

	for(auto& pair: scheduled_queries) {
		pair.second.set_error("stopping service");
	}

	for(auto& query: queries) {
		query.set_error("stopping service");
	}

	close(efd);
	delete[] events;
}

void async_pg::cond_notify() {
	char byte;
	write(_notifiy_fd, &byte, 1);
}

void async_pg::cond_reset() {
	int bytes_available;
	if(ioctl(_wait_fd, FIONREAD, &bytes_available) == 0 && bytes_available > 0) {
		char* dummy = new char[bytes_available];
		read(_wait_fd, dummy, bytes_available);
		delete[] dummy;
	}
}