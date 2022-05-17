#ifndef VECTORCLOCK_H
#define VECTORCLOCK_H

#include <unordered_map>
#include <vector>
#include "lockframe.hpp"

typedef int VectorClockValue;
typedef struct {
    ThreadID thread_id;
    int value;
} Epoch;
class VectorClock {
    private:
        std::unordered_map<ThreadID, VectorClockValue> _vector_clock = {};
    public:
        VectorClock();
        VectorClock(ThreadID increment_thread_id);
        VectorClockValue find(ThreadID thread_id);
        std::vector<Epoch> find_all();
        VectorClock merge(VectorClock vector_clock);
        void set(ThreadID thread_id, VectorClockValue);
        void increment(ThreadID thread_id);
};

#endif