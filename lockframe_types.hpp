#ifndef LOCKFRAME_TYPES_H
#define LOCKFRAME_TYPES_H

#include <string>

typedef int ThreadID;
typedef int TracePosition;
typedef int ResourceName;
#ifdef COLLECT_STATISTICS
typedef std::string StatisticKey;
typedef std::string StatisticValue;
#endif
typedef struct
{
    ResourceName resource_name;
    TracePosition trace_position;
    ThreadID thread_id_1;
    ThreadID thread_id_2;
} DataRace;

#ifdef COLLECT_STATISTICS
typedef struct
{
    StatisticKey statistics_key;
    StatisticValue statistics_value;
} StatisticReport;
#endif
#endif