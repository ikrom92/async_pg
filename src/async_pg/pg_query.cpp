#include "pg_query.hpp"

pg_query::pg_query() {}

pg_query::pg_query(const std::string& sql) : _sql(sql) {}

pg_query::pg_query(std::string&& sql) : _sql(sql) {}

pg_query::pg_query(const std::string& sql, const std::list<pg_param>& params) :
	_sql(sql), _params(params) {}

pg_query::pg_query(std::string&& sql, std::list<pg_param>&& params) :
	_sql(sql), _params(params) {}

pg_query::pg_query(const std::string& name, const std::string& sql, const std::list<pg_param>& params) :
	_name(name), _sql(sql), _params(params) {}

pg_query::pg_query(std::string&& name, std::string&& sql, std::list<pg_param>&& params) :
	_name(name), _sql(sql), _params(params) {}

pg_query::~pg_query() {}

pg_query::pg_query(pg_query&& o) {
	*this = std::move(o);
}

pg_query& pg_query::operator=(pg_query&& o) {
	_name = std::move(o._name);
	_sql = std::move(o._sql);
	_params = std::move(o._params);
	_promise = std::move(o._promise);
	return *this;
}

void pg_query::set_result(std::list<pg_result>&& result) {
	_promise.set_value(std::move(result));
}

void pg_query::set_error(const std::string& error) {
	_promise.set_exception(std::make_exception_ptr(std::runtime_error(error)));
}

void pg_query::set_error(std::string&& error) {
	_promise.set_exception(std::make_exception_ptr(std::runtime_error(std::move(error))));
}

std::future<std::list<pg_result>> pg_query::get_future() {
	return _promise.get_future();
}