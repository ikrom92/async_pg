#include "pg_param.hpp"


pg_param::pg_param(pg_param&& rhs) {
	*this = std::move(rhs);
}

pg_param::pg_param(const pg_param& rhs) {
	*this = rhs;
}

pg_param& pg_param::operator=(pg_param&& rhs) {
	_blob_s8 = std::move(rhs._blob_s8);
	_blob_u8 = std::move(rhs._blob_u8);
	_text = std::move(rhs._text);
	return *this;
}

pg_param& pg_param::operator=(const pg_param& rhs) {
	_blob_s8 = rhs._blob_s8;
	_blob_u8 = rhs._blob_u8;
	_text = rhs._text;
	return *this;
}

const void* pg_param::data() const {
	if (_text.size()) {
		return _text.data();
	}
	else if (_blob_s8.size()) {
		return _blob_s8.data();
	}
	else if (_blob_u8.size()) {
		return _blob_u8.data();
	}
	return nullptr;
}

std::size_t pg_param::size() const {
	return _text.size() | _blob_s8.size() | _blob_u8.size();
}

bool pg_param::is_binary() const {
	return _text.empty();
}


bool is_le() {
	static int endiannes = 0; // 1 - bigendiann, 2 - littleendian
	
	if( endiannes == 0 ) {
		short n = 0x0001;
		char* buf = (char*) &n;
		if( buf[0] == 0 ) {
			endiannes = 1;
		}
		else {
			endiannes = 2;
		}
	}
	return endiannes == 2;
}

pg_param pg_param::boolean(bool val) {
	return pg_param::text(val ? "t" : "f");
}

pg_param pg_param::int16(int16_t number) {
	if (is_le()) {
		number = number << 8 | ((number >> 8) & 0x00FF);
	}
	pg_param p;
	p._blob_u8.assign((uint8_t*)&number, (uint8_t*)&number + 2);
	return p;
}

pg_param pg_param::int32(int32_t number) {
	if (is_le()) {
		number =
		number << 24 |
		((number << 8) & 0x00FF0000) |
		((number >> 8) & 0x0000FF00) |
		((number >> 24) & 0x000000FF);
	}
	pg_param p;
	p._blob_u8.assign((uint8_t*)&number, (uint8_t*)&number + 4);
	return p;
}

pg_param pg_param::int64(int64_t number) {
	if (is_le()) {
		number =
		number << 56 |
		((number << 40) & 0x00FF000000000000) |
		((number << 24) & 0x0000FF0000000000) |
		((number << 8) & 0x000000FF00000000) |
		((number >> 8) & 0x00000000FF000000) |
		((number >> 24) & 0x0000000000FF0000) |
		((number >> 40) & 0x000000000000FF00) |
		((number >> 56) & 0x00000000000000FF);
	}
	pg_param p;
	p._blob_u8.assign((uint8_t*)&number, (uint8_t*)&number + 8);
	return p;
}

pg_param pg_param::uint16(uint16_t number) {
	if (is_le()) {
		number = number << 8 | number >> 8;
	}
	pg_param p;
	p._blob_u8.assign((uint8_t*)&number, (uint8_t*)&number + 2);
	return p;
}

pg_param pg_param::uint32(uint32_t number) {
	if (is_le()) {
		number =
		number << 24 |
		((number << 8) & 0x00FF0000) |
		((number >> 8) & 0x0000FF00) |
		((number >> 24) & 0x000000FF);
	}
	pg_param p;
	p._blob_u8.assign((uint8_t*)&number, (uint8_t*)&number + 4);
	return p;
}

pg_param pg_param::uint64(uint64_t number) {
	if (is_le()) {
		number =
		number << 56 |
		((number << 40) & 0x00FF000000000000) |
		((number << 24) & 0x0000FF0000000000) |
		((number << 8) & 0x000000FF00000000) |
		((number >> 8) & 0x00000000FF000000) |
		((number >> 24) & 0x0000000000FF0000) |
		((number >> 40) & 0x000000000000FF00) |
		((number >> 56) & 0x00000000000000FF);
	}
	pg_param p;
	p._blob_u8.assign((uint8_t*)&number, (uint8_t*)&number + 8);
	return p;
}

pg_param pg_param::text(const std::string& text) {
	pg_param p;
	p._text = text;
	return p;
}

pg_param pg_param::text(std::string&& text) {
	pg_param p;
	p._text = std::move(text);
	return p;
}

pg_param pg_param::blob(const std::vector<char>& blob) {
	pg_param p;
	p._blob_s8 = blob;
	return p;
}

pg_param pg_param::blob(std::vector<char>&& blob) {
	pg_param p;
	p._blob_s8 = std::move(blob);
	return p;
}

pg_param pg_param::blob(const std::vector<uint8_t>& blob) {
	pg_param p;
	p._blob_u8 = blob;
	return p;
}

pg_param pg_param::blob(std::vector<uint8_t>&& blob) {
	pg_param p;
	p._blob_u8 = std::move(blob);
	return p;
}