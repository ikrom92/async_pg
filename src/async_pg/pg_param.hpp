#pragma once

#include <cstdint>
#include <string>
#include <vector>

    
class pg_param {
public:
	pg_param() = default;
	
	pg_param(const pg_param& other);
	pg_param& operator=(const pg_param& other);
	
	pg_param(pg_param&& other);
	pg_param& operator=(pg_param&& other);
	
	static pg_param boolean(bool val);
	
	static pg_param int16(int16_t val);
	static pg_param int32(int32_t val);
	static pg_param int64(int64_t val);
	static pg_param uint16(uint16_t val);
	static pg_param uint32(uint32_t val);
	static pg_param uint64(uint64_t val);
	
	static pg_param text(const std::string& val);
	static pg_param text(std::string&& val);
	
	static pg_param blob(const std::vector<char>& val);
	static pg_param blob(std::vector<char>&& val);
	static pg_param blob(const std::vector<uint8_t>& val);
	static pg_param blob(std::vector<uint8_t>&& val);
	
	const void* data() const;
	std::size_t size() const;
	bool is_binary() const;
	
private:
	std::string _text;
	std::vector<char> _blob_s8;
	std::vector<uint8_t> _blob_u8;
};    
