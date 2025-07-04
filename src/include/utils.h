#pragma once

#include <base/log.h>

/* Decor */
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define CYAN "\033[96m"

#define BG_RED "\033[41m"

#define UNDERLINE "\033[4m"
#define BOLD "\033[1m"

#define RESET "\033[0m"

/* Logging */
#define TODO(message) Genode::log(CYAN "[TODO] ", message)

/* CRC */
/* INFO
   Source:
   https://github.com/genodelabs/genode/blob/f1e85c0db8023ce481a40f85d4cba03f3dc63b27/repos/gems/src/app/gpt_write/util.cc#L140
*/
/**
 * @brief Simple bitwise CRC32 checking.
 * @param buf 					pointer to buffer containing data
 * @param size 					length of buffer in bytes
 * @return uint32_t 		crc32 of the data	
 */
inline Genode::uint32_t crc32(void const * const buf, Genode::size_t size)
{
	Genode::uint8_t const *p = static_cast<Genode::uint8_t const*>(buf);
	Genode::uint32_t crc = ~0U;

	while (size--) {
		crc ^= *p++;
		for (Genode::uint32_t j = 0; j < 8; j++)
			crc = (-Genode::int32_t(crc & 1) & 0xedb88320) ^ (crc >> 1);
	}

	return crc ^ ~0U;
}
