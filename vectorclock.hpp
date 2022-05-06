#ifndef VECTORCLOCK_H
#define VECTORCLOCK_H

#include <unordered_map>
#include <vector>
#include "lockframe.hpp"

typedef int Epoch;
typedef struct {
    ThreadID thread_id;
    Epoch epoch;
} ThreadEpochPair;

class VectorClock {
    private:
        std::unordered_map<ThreadID, Epoch> _vector_clock = {};
    public:
        Epoch* find(ThreadID thread_id);
        Epoch find_or(ThreadID thread_id, Epoch fallback);
        std::vector<ThreadEpochPair> find_all();
        VectorClock merge(VectorClock vector_clock);
        void set(ThreadID thread_id, Epoch epoch);
        void increment(ThreadID thread_id);
};

#endif