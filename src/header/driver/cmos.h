#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>
#include <stdbool.h>
#include "header/syscall.h"

bool cmos_read_time(struct TimeInfo *out);

#endif
