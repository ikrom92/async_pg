#include "pg_result.hpp"
#include <sstream>
#include <iomanip>

pg_result::pg_result(PGresult* res) : _res(res) {}

pg_result::~pg_result() {
	if (_res) {
		PQclear(_res);
		_res = nullptr;
	}
}

pg_result::pg_result(pg_result&& o) {
	_res = o._res;
	o._res = nullptr;
}

pg_result& pg_result::operator=(pg_result&& o) {

	// protect from self assignment
	if (this == &o) {
		return *this;
	}

	// clear old PGresult
	if (_res) {
		PQclear(_res);
	}

	_res = o._res;
	o._res = nullptr;
	return *this;
}

void pg_result::check() {}

int pg_result::rows_count() {
	if (_res) {
		return PQntuples(_res);
	}
	return 0;
}

int pg_result::cols_count() {
	if (_res) {
		return PQnfields(_res);
	}
	return 0;
}

const char* pg_result::col_name(int col_number) {
	if (_res) {
		return PQfname(_res, col_number);
	}
	return nullptr;
}

Oid pg_result::col_oid_number(int col_number) {
	if (_res) {
		return PQftype(_res, col_number);
	}
	return 0;
}

int pg_result::col_number(const char* col_name) {
	if (_res) {
		return PQfnumber(_res, col_name);
	}
	return -1;
}

const char* pg_result::get_value(int row_number, int col_number) {
	if (_res) {
		return PQgetvalue(_res, row_number, col_number);
	}
	return nullptr;
}

bool pg_result::is_null(int row_number, int col_number) {
	if (_res) {
		return PQgetisnull(_res, row_number, col_number);
	}
	return true;
}

int pg_result::rows_affected() {
	if (_res) {
		 return std::atol(PQcmdTuples(_res));
	}
	return 0;
}

std::string pg_result::dump() {

	std::stringstream ss;
	int n_rows = rows_count();
	int n_cols = cols_count();
	for (int i = 0; i < n_rows; ++i) {
		
		if (i == 0) {
			for(int j = 0; j < n_cols; ++j) {
				ss << std::left << col_name(j)
				<< "(" << PQftype(_res, j) << ":" << PQfformat(_res, j)  << std::setw(12) << ")";
			}
			ss << std::endl;
			ss << std::string(n_cols * 13, '-');
			ss << std::endl;
		}
		
		for(int j = 0; j < n_cols; ++j) {
			ss << std::setw(12) << std::left << (PQgetisnull(_res, i, j) ? "[NULL]" : get_value(i, j));
		}
		ss << std::endl;
	}
	return ss.str();
	
}