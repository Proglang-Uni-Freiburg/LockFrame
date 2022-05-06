#ifndef LOCKFRAME_TYPES_H
#define LOCKFRAME_TYPES_H

#include <string>

typedef int ThreadID;
typedef int TracePosition;
typedef std::string ResourceName;
typedef struct {
    ResourceName resource_name;
    TracePosition trace_position;
    ThreadID thread_id_1;
    ThreadID thread_id_2;
} DataRace;

#endif