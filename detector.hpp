#ifndef DETECTOR_H
#define DETECTOR_H

#include "lockframe_types.hpp"

class LockFrame;
class Detector {
    public:
        LockFrame* lockframe;
        virtual void read_event(ThreadID, TracePosition, ResourceName) {}
        virtual void write_event(ThreadID, TracePosition, ResourceName) {}
        virtual void acquire_event(ThreadID, TracePosition, ResourceName) {}
        virtual void release_event(ThreadID, TracePosition, ResourceName) {}
        virtual void fork_event(ThreadID, TracePosition, ThreadID) {}
        virtual void join_event(ThreadID, TracePosition, ThreadID) {}
        virtual void get_races() {}
};

#endif