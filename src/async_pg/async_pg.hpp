#include <future>
#include <list>
#include <thread>
#include <mutex>
#include <map>

#include "pg_param.hpp"
#include "pg_result.hpp"
#include "pg_query.hpp"

class async_pg {
public:
	async_pg(std::string connection_string);
	async_pg(std::string ip, std::string port, std::string dbname, std::string user, std::string password, std::string ssl_mode);
	async_pg(std::map<std::string, std::string> params);
	~async_pg();

	void start(int n_connections);
	void stop();

	std::future<std::list<pg_result>> execute(
		std::string&& sql,
		std::list<pg_param>&& params = {});

	std::future<std::list<pg_result>> execute(
		const std::string& sql,
		const std::list<pg_param>& params = {});

	std::future<std::list<pg_result>> execute_prepared(
		std::string&& name,
		std::string&& sql,
		std::list<pg_param>&& params = {});

	std::future<std::list<pg_result>> execute_prepared(
		const std::string& name,
		const std::string& sql,
		const std::list<pg_param>& params = {});

private:
	void process(int n_connections);
	bool _running;
	std::thread _thr;
	std::mutex _mtx;
	std::list<pg_query> _queries;
	std::map<std::string, std::string> _connection_params;
};