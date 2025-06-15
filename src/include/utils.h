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
