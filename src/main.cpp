#include <iostream>
#include "async_pg/async_pg.hpp"
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <list>
#include <atomic>
#include <mutex>
#include <condition_variable>

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
std::mutex lock;
std::condition_variable cv;

void signal_handler(int s) {
	running = false;
	cv.notify_one();
}


std::atomic<int> total = 0;

int main() {

	std::signal(SIGKILL, signal_handler);
	std::signal(SIGHUP, signal_handler);
	std::signal(SIGINT, signal_handler);
	std::signal(SIGABRT, signal_handler);
	std::signal(SIGQUIT, signal_handler);

	async_pg pg("10.0.2.2", "5432", "workly", "postgres", "123", "disable");
	pg.start(10);

	std::thread threads[100];
	
	for(int j = 0; j < 100; ++j) {
		threads[j] = std::thread([&pg, j]{
			int i = 0; 
			while (running) {

				// std::string command = "select id, device_type_id, company_id, alias, serial_number from w_device";

				std::string command, name;

				if (i % 3 == 0) {
					name = "get_device";
					command = "select * from w_device";
				}
				else if (i % 3 == 1) {
					name = "get_device_cmds";
					command = "select * from w_device_cmds";
				}
				else {
					name = "get_faces";
					command = "select id, person_id from w_faces";
				}

				i++;

				// std::getline(std::cin, command);

				// if (command == "quit") {
				// 	break;
				// }

				total++;

				auto f = pg.execute_prepared(name, command);
				f.wait();
				auto results = f.get();
				for (auto& r : results) {
					std::cout << j << ": " << i << " --------------------------" << std::endl;
					std::cout << r.dump() << std::endl;
					std::cout << "--------------------------" << std::endl;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		});
	}

	std::unique_lock<std::mutex> l(lock);
	cv.wait(l);

	std::cout << "stopping..."<< std::endl;
	for(int j = 0; j < 100; ++j) {
		if (threads[j].joinable()) {
			threads[j].join();
		}
	}

	std::cout << "total = " << total << std::endl;
	pg.stop();
	std::cout << "Bye bye!" << std::endl;
}