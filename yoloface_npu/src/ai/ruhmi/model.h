#ifndef MODEL_H_
#define MODEL_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// NPU unit addresses
#include "sub_0000_tensors.h"

// Arenas for NPU units
extern uint8_t *sub_0000_arena;

void RunModel(bool clean_outputs);

// Model input pointers
int8_t* GetModelInputPtr_image_input();

// Model output pointers
int8_t* GetModelOutputPtr_Identity_70275();
int8_t* GetModelOutputPtr_Identity_1_70284();

#ifdef __cplusplus
}
#endif

#endif // MODEL_H_
