/* host-machine.h -- the parts of the machine that are not the point.
 *
 * See tools/host-machine.c.  Only the two hosted tools include this.
 */

#ifndef HOST_MACHINE_H
#define HOST_MACHINE_H

#include <stddef.h>

#include "kernel.h"

void        capture_begin(void);
void        capture_end(void);
int         capture_over(void);
const char *capture_text(void);
size_t      capture_len(void);

#endif
