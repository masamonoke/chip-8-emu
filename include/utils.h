#ifndef UTILS_H
#define UTILS_H

#include <log.h>

#define UNUSED(x) (void)(x)

#define NOT_IMPLEMENTED(func_name) (log_error("%s not implemented!", func_name))

#endif // UTILS_H
