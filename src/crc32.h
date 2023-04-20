#ifndef _CRC32_H_
#define _CRC32_H_

#include <cstdint>

namespace crc32 {
	uint32_t compute(const char* fname, uint32_t start_crc = 0);
}

#endif //_CRC32_H_

