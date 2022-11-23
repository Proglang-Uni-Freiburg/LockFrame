#ifndef VECTORCLOCK_TYPES_H
#define VECTORCLOCK_TYPES_H

#include "lockframe.hpp"

typedef int VectorClockValue;
typedef struct {
    ThreadID thread_id;
    VectorClockValue value;
} Epoch;

#endif