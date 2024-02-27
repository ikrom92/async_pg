#include <iostream>
#include "async_pg/async_pg.hpp"
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <list>



PGconn* connect() {
	// "10.0.2.2", "5432", "workly", "postgres", "123", "disable"
	char** keywords = new char* [7];
	char** values = new char* [7];
	for (int i = 0; i < 6; ++i) {
		keywords[i] = new char[50];
		values[i] = new char[50];
	}

	std::strncpy(keywords[0], "hostaddr", 50);
	std::strncpy(keywords[1], "port", 50);
	std::strncpy(keywords[2], "dbname", 50);
	std::strncpy(keywords[3], "user", 50);
	std::strncpy(keywords[4], "password", 50);
	std::strncpy(keywords[5], "sslmode", 50);
	keywords[6] = nullptr;

	std::strncpy(values[0], "10.0.2.2", 50);
	std::strncpy(values[1], "5432", 50);
	std::strncpy(values[2], "workly", 50);
	std::strncpy(values[3], "postgres", 50);
	std::strncpy(values[4], "123", 50);
	std::strncpy(values[5], "disable", 50);
	values[6] = nullptr;

	PGconn* conn = PQconnectStartParams(keywords, values, 0);
	if (conn) {
		ConnStatusType st = PQstatus(conn);
		if (st == CONNECTION_BAD) {
			printf("PQstatus -> CONNECTION_BAD: %s\n", PQerrorMessage(conn));
			PQfinish(conn);
			conn = nullptr;
		}
		else {
			PQsetnonblocking(conn, 1);
		}
	}
	else {
		printf("PQconnectStartParams -> nullptr\n");
	}

	for (int i = 0; i < 6; ++i) {
		delete[] keywords[i];
		delete[] values[i];
	}

	delete[] keywords;
	delete[] values;
	return conn;
}

void readResult(PGconn* conn) {

	while (true) {
		PGresult* res = PQgetResult(conn);
		if (!res) {
			printf("PQgetResult returns nullptr\n");
			return;
		}

		ExecStatusType st = PQresultStatus(res);
		if (st == ExecStatusType::PGRES_TUPLES_OK) {
			printf("\nPGRES_TUPLES_OK: Start\n\n");
			int n_fields = PQnfields(res);
			int n_tuples = PQntuples(res);
			for (int t = -1; t < n_tuples; ++t) {
				for (int f = 0; f < n_fields; ++f) {
					if (t == -1) {
						printf("%s\t", PQfname(res, f));
					}
					else {
						printf("%s\t", PQgetvalue(res, t, f));
					}
				}
				if (t == -1) {
					printf("\n------------------------------");
				}
				printf("\n");
			}
			printf("\nPGRES_TUPLES_OK: Done\n\n");
		}
		else if (st == ExecStatusType::PGRES_COMMAND_OK) {
			printf("PGRES_COMMAND_OK\n\n");
		}
		else if (st == ExecStatusType::PGRES_FATAL_ERROR) {
			printf("PGRES_FATAL_ERROR: %s\n", PQerrorMessage(conn));
		}
		else {
			printf("PQresultStatus -> %d\n", st);
		}


		PQclear(res);
	}

}

bool running = true;

void signal_handler(int s) {
	running = false;
}

int test1() {

	std::signal(SIGKILL, signal_handler);
	std::signal(SIGHUP, signal_handler);
	std::signal(SIGINT, signal_handler);
	std::signal(SIGABRT, signal_handler);
	std::signal(SIGQUIT, signal_handler);

	PGconn* conn = connect();
	if (conn == nullptr) {
		return -1;
	}

	int efd = epoll_create1(0);
	if (efd == -1) {
		printf("epoll_create1() failed. error = %d\n", errno);
		PQfinish(conn);
		return -2;
	}

	epoll_event events[4];

	while (running) {

		epoll_event e;
		e.events = EPOLLERR;
		int s = PQsocket(conn);

		PostgresPollingStatusType st = PQconnectPoll(conn);
		if (st == PGRES_POLLING_FAILED) {
			printf("PQconnectPoll -> PGRES_POLLING_FAILED. %s\n", PQerrorMessage(conn));
		}
		else if (st == PGRES_POLLING_OK) {
			printf("PQconnectPoll -> PGRES_POLLING_OK\n");
		}
		else if (st == PGRES_POLLING_READING) {
			printf("PQconnectPoll -> PGRES_POLLING_READING\n");
			e.events |= EPOLLIN;
		}
		else if (st == PGRES_POLLING_WRITING) {
			printf("PQconnectPoll -> PGRES_POLLING_WRITING\n");
			e.events |= EPOLLOUT;
		}

		if (epoll_ctl(efd, EPOLL_CTL_ADD, s, &e) == -1) {
			printf("epoll_ctl(add) failed: %d\n", errno);
		}

		int n = epoll_wait(efd, events, 4, 100);
		printf("n events %d\n", n);
		if (n > 0) {
			e = events[0];
			if (e.events & EPOLLIN) {
				printf("EPOLLIN\n");
			}
			if (e.events & EPOLLOUT) {
				printf("EPOLLOUT\n");
			}
			if (e.events & EPOLLERR) {
				printf("EPOLLERR\n");
			}
		}
		if (epoll_ctl(efd, EPOLL_CTL_DEL, s, nullptr) == -1) {
			printf("epoll_ctl(del) failed: %d\n", errno);
		}

		printf("\n");

		sleep(1);
	}

	printf("closing...\n");
	PQfinish(conn);
	return 0;
}

int test2() {

	std::signal(SIGKILL, signal_handler);
	std::signal(SIGHUP, signal_handler);
	std::signal(SIGINT, signal_handler);
	std::signal(SIGABRT, signal_handler);
	std::signal(SIGQUIT, signal_handler);

	PGconn* conn = connect();
	if (conn == nullptr) {
		return -1;
	}

	int efd = epoll_create1(0);
	if (efd == -1) {
		printf("epoll_create1() failed. error = %d\n", errno);
		PQfinish(conn);
		return -2;
	}

	epoll_event events[4];
	bool sending_command = false;

	while (running) {

		epoll_event e;
		e.events = EPOLLERR;
		int s = PQsocket(conn);

		if (sending_command) {
			if (PQisBusy(conn)) {
				printf("PQisBusy -> 1\n");
				e.events |= EPOLLIN | EPOLLOUT;
			}
			else {
				printf("PQisBusy -> 0\n");
				while (true) {
					PGresult* result = PQgetResult(conn);
					if (result) {
						printf("Got result\n\n\n");
						PQclear(result);
					}
					else {
						break;
					}
				}
				
				sending_command = false;
			}
		}
		else {
			PostgresPollingStatusType st = PQconnectPoll(conn);
			if (st == PGRES_POLLING_FAILED) {
				printf("PQconnectPoll -> PGRES_POLLING_FAILED. %s\n", PQerrorMessage(conn));
				if (PQresetStart(conn)) {
					printf("PQresetStart -> OK\n");
				}
				else {
					printf("PQresetStart -> FALSE: %s\n", PQerrorMessage(conn));
				}
			}
			else if (st == PGRES_POLLING_OK) {
				printf("PQconnectPoll -> PGRES_POLLING_OK\n");
				if (PQsendQuery(conn, "SELECT 1")) {
					printf("PQsendQuery -> OK\n");
					sending_command = true;
					continue;
				}
				else {
					printf("PQsendQuery -> FALSE\n");
				}
			}
			else if (st == PGRES_POLLING_READING) {
				printf("PQconnectPoll -> PGRES_POLLING_READING\n");
				e.events |= EPOLLIN;
			}
			else if (st == PGRES_POLLING_WRITING) {
				printf("PQconnectPoll -> PGRES_POLLING_WRITING\n");
				e.events |= EPOLLOUT;
			}
		}

		if (epoll_ctl(efd, EPOLL_CTL_ADD, s, &e) == -1) {
			printf("epoll_ctl(add) failed: %d\n", errno);
		}

		int n = epoll_wait(efd, events, 4, 100);
		printf("epoll_events %d\n", n);
		if (n > 0) {
			e = events[0];
			if (e.events & EPOLLIN) {
				if (PQisBusy(conn)) {
					printf("EPOLLIN > PQisBusy -> YES\n");
					PQconsumeInput(conn);
					PQflush(conn);
				}
				else {
					printf("EPOLLIN > PQisBusy -> NO\n");
				}
			}
			if (e.events & EPOLLOUT) {
				if (PQisBusy(conn)) {
					printf("EPOLLOUT > PQisBusy -> YES\n");
					PQflush(conn);
				}
				else {
					printf("EPOLLOUT > PQisBusy -> NO\n");
				}
			}
			if (e.events & EPOLLERR) {
				printf("EPOLLERR\n");
			}
		}
		if (epoll_ctl(efd, EPOLL_CTL_DEL, s, nullptr) == -1) {
			printf("epoll_ctl(del) failed: %d\n", errno);
		}

		printf("\n");

		sleep(1);
	}

	printf("closing...\n");
	PQfinish(conn);
	return 0;
}

int test3() {

	std::signal(SIGKILL, signal_handler);
	std::signal(SIGHUP, signal_handler);
	std::signal(SIGINT, signal_handler);
	std::signal(SIGABRT, signal_handler);
	std::signal(SIGQUIT, signal_handler);

	PGconn* conn = connect();
	if (conn == nullptr) {
		return -1;
	}

	int efd = epoll_create1(0);
	if (efd == -1) {
		printf("epoll_create1() failed. error = %d\n", errno);
		PQfinish(conn);
		return -2;
	}

	epoll_event events[4];
	while (running) {

		epoll_event e;
		e.events = EPOLLERR;
	
		if (PQisBusy(conn)) {
			e.events |= EPOLLIN | EPOLLOUT;
		}
		else {
			PostgresPollingStatusType st = PQconnectPoll(conn);
			if (st == PGRES_POLLING_FAILED) {
				printf("PQconnectPoll -> PGRES_POLLING_FAILED. %s\n", PQerrorMessage(conn));
				if (PQresetStart(conn)) {
					printf("PQresetStart -> OK\n");
				}
				else {
					printf("PQresetStart -> FALSE: %s\n", PQerrorMessage(conn));
				}
			}
			else if (st == PGRES_POLLING_OK) {
				printf("PQconnectPoll -> PGRES_POLLING_OK\n");
				if (PQsendQuery(conn, "SELECT dev.id, dev.in_mode, dev.out_mode, dev.time_zone_adj, dev.company_id, dev.last_inout_time FROM w_device AS dev")) {
					printf("PQsendQuery -> OK\n");
					e.events |= EPOLLIN | EPOLLOUT;
				}
				else {
					printf("PQsendQuery -> FALSE\n");
				}
			}
			else if (st == PGRES_POLLING_READING) {
				printf("PQconnectPoll -> PGRES_POLLING_READING\n");
				e.events |= EPOLLIN;
			}
			else if (st == PGRES_POLLING_WRITING) {
				printf("PQconnectPoll -> PGRES_POLLING_WRITING\n");
				e.events |= EPOLLOUT;
			}
		}

		int s = PQsocket(conn);
		if (epoll_ctl(efd, EPOLL_CTL_ADD, s, &e) == -1) {
			printf("epoll_ctl(add) failed: %d\n", errno);
		}

		int n = epoll_wait(efd, events, 4, 100);
		printf("epoll_events %d\n", n);
		if (n > 0) {
			e = events[0];
			if (e.events & EPOLLIN) {
				if (PQisBusy(conn)) {
					printf("EPOLLIN > PQisBusy -> YES\n");
					printf("PQconsumeInput: %d\n", PQconsumeInput(conn));
					printf("PQflush %d\n", PQflush(conn));
					if (!PQisBusy(conn)) {
						printf("EPOLLIN > PQisBusy -> NO. readResult()\n");
						readResult(conn);
					}
				}
				else {
					printf("EPOLLIN > PQisBusy -> NO\n");
				}
			}
			if (e.events & EPOLLOUT) {
				if (PQisBusy(conn)) {
					printf("EPOLLOUT > PQisBusy -> YES\n");
					printf("PQflush %d\n", PQflush(conn));
					if (!PQisBusy(conn)) {
						readResult(conn);
					}
				}
				else {
					printf("EPOLLOUT > PQisBusy -> NO\n");
				}
			}
			if (e.events & EPOLLERR) {
				printf("EPOLLERR\n");
			}
		}
		if (epoll_ctl(efd, EPOLL_CTL_DEL, s, nullptr) == -1) {
			printf("epoll_ctl(del) failed: %d\n", errno);
		}

		printf("\n");

		sleep(1);
	}

	printf("closing...\n");
	PQfinish(conn);
	return 0;
}


int main() {
	async_pg pg("10.0.2.2", "5432", "workly", "postgres", "123", "disable");
	pg.start(10);

	while (running) {

		std::string command;
		std::cin >> command;

		if (command == "quit") {
			break;
		}

		auto f = pg.execute(command);
		f.wait();
		auto results = f.get();
		for (auto& r : results) {
			std::cout << "--------------------------" << std::endl;
			std::cout << r.dump() << std::endl;
			std::cout << "--------------------------" << std::endl;
		}
	}

	pg.stop();
	std::cout << "Bye bye!" << std::endl;
}