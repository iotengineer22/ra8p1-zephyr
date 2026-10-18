#ifndef __SUB_0000_INVOKE_H__
#define __SUB_0000_INVOKE_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Declare arenas
extern uint8_t *sub_0000_arena;
extern uint8_t *sub_0000_fast_scratch;

int sub_0000_invoke(bool clean_outputs);

#ifdef __cplusplus
}
#endif

#endif // __SUB_0000_INVOKE_H__
