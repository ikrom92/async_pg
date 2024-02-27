#include "pg_connection.hpp"


pg_connection::pg_connection() :
	_conn(nullptr),
	_async_state(async_state_t::connection_failed)
{}

pg_connection::~pg_connection() {
	if (_conn) {
		PQfinish(_conn);
		_conn = nullptr;
	}
}

bool pg_connection::start_connect(const std::map<std::string, std::string>& params) {

	if (_conn) {
		PQfinish(_conn);
		_conn = nullptr;
	}

	char** keywords = new char* [params.size() + 1];
	char** values = new char*[params.size() + 1];
	int idx = 0;
	for(auto& p: params) {
		keywords[idx] = (char*)p.first.c_str();
		values[idx] = (char*)p.second.c_str();
		++idx;
	}
	keywords[idx] = nullptr;
	values[idx] = nullptr;
	
	PGconn* conn = PQconnectStartParams(keywords, values, 0);
	delete[] keywords;
	delete[] values;
	
	if (!conn) {
		_last_error = "PGconn allocation failed";
		_async_state = async_state_t::connection_failed;
		return false;
	}

	if (PQstatus(conn) == CONNECTION_BAD) {
		_last_error = PQerrorMessage(conn);
		PQfinish(conn);
		_async_state = async_state_t::connection_failed;
		return false;
	}
	
	PQsetnonblocking(conn, 1);
	_conn = conn;
	_async_state = async_state_t::connecting;
	return true;
}

bool pg_connection::start_reset() {

	if (!_conn) {
		_last_error = "reset cannot be called on null connection";
		_async_state = async_state_t::connection_failed;
		return false;
	}

	if (PQresetStart(_conn) == 0) {
		_last_error = PQerrorMessage(_conn);
		_async_state = async_state_t::connection_abort;
		return false;
	}
	_async_state = async_state_t::resetting;
	return true;
}

PostgresPollingStatusType pg_connection::connectPoll() {
	PostgresPollingStatusType s = PQconnectPoll(_conn);
	if (_async_state == async_state_t::connecting) {
		if (s == PostgresPollingStatusType::PGRES_POLLING_OK) {
			_async_state = async_state_t::idle;
			_prepared_statements.clear();
		}
		else if (s == PostgresPollingStatusType::PGRES_POLLING_FAILED) {
			_async_state = async_state_t::connection_failed;
			_last_error = PQerrorMessage(_conn);
		}
	}
	return s;
}

PostgresPollingStatusType pg_connection::resetPoll() {
	PostgresPollingStatusType s = PQresetPoll(_conn);
	if (_async_state == async_state_t::resetting) {
		if (s == PostgresPollingStatusType::PGRES_POLLING_OK) {
			_async_state = async_state_t::idle;
			_prepared_statements.clear();
		}
		else if (s == PostgresPollingStatusType::PGRES_POLLING_FAILED) {
			_async_state = async_state_t::connection_abort;
			_last_error = PQerrorMessage(_conn);
		}
	}
	return s;
}


bool pg_connection::start_send_query(const std::string& sql, const std::list<pg_param>& params) {

	if (_async_state != async_state_t::idle) {
		return false;
	}

	if (params.empty()) {
		if (PQsendQuery(_conn, sql.c_str()) == 0) {
			_last_error = PQerrorMessage(_conn);
			return false;
		}
	}
	else {
		int n_params = (int) params.size();
		char** values = new char* [n_params];
		int* lengths = new int[n_params];
		int* formats = new int[n_params];
		int i = 0;
		for (auto& p : params) {
			values[i] = (char*)p.data();
			lengths[i] = (int)p.size();
			formats[i] = (int)p.is_binary();
			++i;
		}

		int r = PQsendQueryParams(_conn, sql.c_str(), n_params, nullptr, values, lengths, formats, 0);
		delete[] values;
		delete[] lengths;
		delete[] formats;
		if (r == 0) {
			_last_error = PQerrorMessage(_conn);
			return false;
		}
	}

	//	After sending any command or data on a nonblocking connection, call PQflush. 
	// 	Returns 1 if it was unable to send all the data in the send queue yet 
	if (PQflush(_conn) == 1) {
		_need_flush = true;
	}

	_async_state = async_state_t::executing_query;
	return true;
}

bool pg_connection::start_send_prepared_query(const std::string& name, const std::list<pg_param>& params) {

	if (_async_state != async_state_t::idle) {
		return false;
	}

	int n_params = (int) params.size();
	char** values = new char* [n_params];
	int* lengths = new int[n_params];
	int* formats = new int[n_params];
	int i = 0;
	for (auto& p : params) {
		values[i] = (char*)p.data();
		lengths[i] = (int)p.size();
		formats[i] = (int)p.is_binary();
		++i;
	}

	int r = PQsendQueryPrepared(_conn, name.c_str(), n_params, values, lengths, formats, 0);
	delete[] values;
	delete[] lengths;
	delete[] formats;
	if (r == 0) {
		_last_error = PQerrorMessage(_conn);
		return false;
	}

	//	After sending any command or data on a nonblocking connection, call PQflush. 
	// 	Returns 1 if it was unable to send all the data in the send queue yet 
	if (PQflush(_conn) == 1) {
		_need_flush = true;
	}

	_async_state = async_state_t::executing_query;
	return true;
}

bool pg_connection::start_send_prepared_statement(const std::string& name, const std::string& sql, const std::list<pg_param>& params) {

	if (_async_state != async_state_t::idle) {
		return false;
	}

	if (PQsendPrepare(_conn, name.c_str(), sql.c_str(), 0, nullptr) == 0) {
		_last_error = PQerrorMessage(_conn);
		return false;
	}


	//	After sending any command or data on a nonblocking connection, call PQflush. 
	// 	Returns 1 if it was unable to send all the data in the send queue yet 
	if (PQflush(_conn) == 1) {
		_need_flush = true;
	}

	_async_state = async_state_t::executing_query;
	return true;
}

bool pg_connection::has_prepared_statement(const std::string& name) {
	return _prepared_statements.count(name) > 0;
}

bool pg_connection::poll_read() {
	// If PQflush() returns 1, wait for the socket to become read- or write-ready. 
	return PQisBusy(_conn) == 1;
}

bool pg_connection::poll_write() {
	return _need_flush;
}

int pg_connection::socket() {
	return PQsocket(_conn);
}

bool pg_connection::get_results(std::list<pg_result>& results) {

	if (_async_state != async_state_t::executing_query) {
		return false;
	}

	//	Returns 1 if a command is busy, that is, PQgetResult would block waiting for input. 
	//	A 0 return indicates that PQgetResult can be called with assurance of not blocking.
	if (PQisBusy(_conn) == 1) {
		return false;
	}

	while (PGresult* res = PQgetResult(_conn)) {
		results.push_back(pg_result(res));
	}

	_need_flush = false;
	_async_state = async_state_t::idle;
	return true;
}

bool pg_connection::get_notifies() {

	//	Returns 1 if a command is busy, that is, PQgetResult would block waiting for input. 
	//	A 0 return indicates that PQgetResult can be called with assurance of not blocking.
	if (PQisBusy(_conn) == 1) {
		return false;
	}

	while (PGnotify* notify = PQnotifies(_conn)) {
		// @todo
	}
	return true;
}

bool pg_connection::read() {

	//	Note! Don't call PQconsumeInput if PQconnectPoll or PQresetPoll 
	//	returns PGRES_POLLING_READING or PGRES_POLLING_WRITING. 
	if (connectPoll() != PostgresPollingStatusType::PGRES_POLLING_OK) {
		return true;
	}

	//	When the main loop detects input ready, it should call PQconsumeInput to read the input. 
	//	It can then call PQisBusy, followed by PQgetResult if PQisBusy returns false (0).
	//	It can also call PQnotifies to detect NOTIFY messages

	//	PQconsumeInput normally returns 1 indicating “no error”, 
	//	but returns 0 if there was some kind of trouble(in which case PQerrorMessage can be consulted).
	//	Note that the result does not say whether any input data was actually collected.
	if (PQconsumeInput(_conn) == 0) {
		_last_error = PQerrorMessage(_conn);
		return false;
	}

	return true;
}

bool pg_connection::write() {

	if (connectPoll() != PostgresPollingStatusType::PGRES_POLLING_OK) {
		return true;
	}

	//	Attempts to flush any queued output data to the server. 
	//	Returns 0 if successful (or if the send queue is empty), 
	//	-1 if it failed for some reason,
	//	or 1 if it was unable to send all the data in the send queue yet 
	//	(this case can only occur if the connection is nonblocking).

	//	After sending any command or data on a nonblocking connection, call PQflush. 
	//	If it returns 1, wait for the socket to become read - or write - ready.
	//	If it becomes write - ready, call PQflush again.
	//	If it becomes read - ready, call PQconsumeInput, then call PQflush again.
	//	Repeat until PQflush returns 0. 
	//	It is necessary to check for read - ready and drain the input with PQconsumeInput, 
	//	because the server can block trying to send us data, e.g., NOTICE messages, and won't read our data until we read its.
	//	Once PQflush returns 0, wait for the socket to be read - ready and then read the response as described above.

	int r = PQflush(_conn);
	if (r == -1) {
		_last_error = PQerrorMessage(_conn);
		return false;
	}
	_need_flush = r == 1;
	return true;
}