#ifndef LOCKFRAME_H
#define LOCKFRAME_H

#include <string>
#include <vector>
#include "lockframe_types.hpp"

class Detector;
class LockFrame {
    public:
        void set_detector(Detector*);
        void read_event(ThreadID, TracePosition, ResourceName);
        void write_event(ThreadID, TracePosition, ResourceName);
        void acquire_event(ThreadID, TracePosition, ResourceName);
        void release_event(ThreadID, TracePosition, ResourceName);
        void fork_event(ThreadID, TracePosition, ThreadID);
        void join_event(ThreadID, TracePosition, ThreadID);
        void report_race(DataRace);
        std::vector<DataRace> get_races();
};

#endif