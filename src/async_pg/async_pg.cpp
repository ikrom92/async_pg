#include "async_pg.hpp"
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <errno.h>
#include <unordered_map>
#include "pg_connection.hpp"

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
}

async_pg::~async_pg() {
	if (_running) {
		stop();
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
	return future;
}

std::future<std::list<pg_result>> async_pg::execute(const std::string& sql, const std::list<pg_param>& params) {

	pg_query query(sql, params);
	auto future = query.get_future();

	std::lock_guard<std::mutex> lock(_mtx);
	_queries.push_back(std::move(query));
	return future;
}

std::future<std::list<pg_result>> async_pg::execute_prepared(std::string&& name, std::string&& sql, std::list<pg_param>&& params) {

	pg_query query(std::move(name), std::move(sql), std::move(params));
	auto future = query.get_future();

	std::lock_guard<std::mutex> lock(_mtx);
	_queries.push_back(std::move(query));
	return future;
}

std::future<std::list<pg_result>> async_pg::execute_prepared(const std::string& name, const std::string& sql, const std::list<pg_param>& params) {

	pg_query query(name, sql, params);
	auto future = query.get_future();

	std::lock_guard<std::mutex> lock(_mtx);
	_queries.push_back(std::move(query));
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
			std::string error = conn->last_error();
			//@todo: log error
		}
	}
	
	int efd = epoll_create1(0);
	epoll_event* events = new epoll_event[n_connections];
	std::unordered_map<int, std::shared_ptr<pg_connection>> scheduled_connections;

	while (true) {

		// schedule events
		for (auto conn: connections) {
			epoll_event event;
			event.events = EPOLLERR;

			// conn->connectPoll() might change async_state of connection
			if (conn->async_state() == pg_connection::async_state_t::connecting) {
				printf("[%02d] async_state_t::connecting\n", conn->id());
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
				printf("[%02d] async_state_t::resetting\n", conn->id());
				PostgresPollingStatusType s = conn->resetPoll();
				if (s == PostgresPollingStatusType::PGRES_POLLING_READING) {
					event.events |= EPOLLIN;
				}
				else if (s == PostgresPollingStatusType::PGRES_POLLING_WRITING) {
					event.events |= EPOLLOUT;
				}
			}

			if (conn->async_state() == pg_connection::async_state_t::connection_failed) {
				printf("[%02d] async_state_t::connection_failed: %s\n", conn->id(), conn->last_error().c_str());
				if (!conn->start_connect(_connection_params)) {
					printf("\t[%02d] start_connect -> %s\n", conn->id(), conn->last_error().c_str());
				}
			}

			if (conn->async_state() == pg_connection::async_state_t::connection_abort) {
				printf("[%02d] async_state_t::connection_abort: %s\n", conn->id(), conn->last_error().c_str());
				if (!conn->start_reset()) {
					printf("\t[%02d] start_reset -> %s\n", conn->id(), conn->last_error().c_str());
				}
			}

			if (conn->async_state() == pg_connection::async_state_t::executing_query) {
				printf("[%02d] async_state_t::executing_query\n", conn->id());
				if (conn->poll_read()) {
					event.events |= EPOLLIN;
				}

				if (conn->poll_write()) {
					event.events |= EPOLLOUT;
				}

			}

			if (conn->async_state() == pg_connection::async_state_t::idle) {
				printf("[%02d] async_state_t::idle\n", conn->id());
				if (!conn->start_send_query("SELECT * FROM w_device")) {
					printf("[%02d] start_send_query -> %s\n", conn->id(), conn->last_error().c_str());
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

		int n_events = epoll_wait(efd, events, n_connections, 100);
		if (n_events == -1) {
			int error = errno;
			// @todo handle error
			printf("[error] epoll_wait -> %d\n", error);
		}

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
						for (auto& r : results) {
							printf("[%02d] got result\n", conn->id());
							// printf("---------------\n%s\n\n", r.dump().c_str());
						}
					}
				}
				
				if (event.events & EPOLLERR) {
					// @todo handle error
					printf("\tEPOLLERR\n");
				}

				if (epoll_ctl(efd, EPOLL_CTL_DEL, event.data.fd, nullptr) == -1) {
					printf("epoll_ctl(del) -> %d\n", errno);
				}
				scheduled_connections.erase(event.data.fd);
			}
			
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}




	close(efd);
	delete[] events;
}