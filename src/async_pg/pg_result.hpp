#pragma once

#include <string>
#include "libpq-fe.h"


class pg_result {
public:
	pg_result(PGresult* res);
	~pg_result();

	pg_result(pg_result&&);
	pg_result& operator=(pg_result&&);

	pg_result(const pg_result&) = delete;
	pg_result& operator=(const pg_result&) = delete;

	void check();
	int rows_count();
	int cols_count();
	
	const char* col_name(int col_number);
	Oid col_oid_number(int col_number);
	int col_number(const char* col_name);
	const char* get_value(int row_number, int col_number);
	bool is_null(int row_number, int col_number);
	int rows_affected();
	
	std::string dump();

private:
	PGresult* _res;
};