#pragma once

#include <future>
#include <string>
#include <list>
#include "pg_param.hpp"
#include "pg_result.hpp"


class pg_query {
public:
	pg_query();
	pg_query(const std::string& sql);
	pg_query(std::string&& sql);
	pg_query(const std::string& sql, const std::list<pg_param>& params);
	pg_query(std::string&& sql, std::list<pg_param>&& params);
	pg_query(const std::string& name, const std::string& sql, const std::list<pg_param>& params);
	pg_query(std::string&& name, std::string&& sql, std::list<pg_param>&& params);
	~pg_query();

	pg_query(const pg_query&) = delete;
	pg_query& operator=(const pg_query&) = delete;

	pg_query(pg_query&&);
	pg_query& operator=(pg_query&&);

	void set_result(std::list<pg_result>&& result);
	void set_error(const std::string& error);
	void set_error(std::string&& error);

	const std::string& name() { return _name; }
	const std::string& sql() { return _sql; }
	const std::list<pg_param>& params() { return _params; }

	std::future<std::list<pg_result>> get_future();

private:
	std::string _name;
	std::string _sql;
	std::list<pg_param> _params;
	std::promise<std::list<pg_result>> _promise;
};