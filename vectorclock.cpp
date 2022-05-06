#include "vectorclock.hpp"

Epoch* VectorClock::find(ThreadID thread_id) {
    auto vc = this->_vector_clock.find(thread_id);

    if(vc == this->_vector_clock.end()) {
        return NULL;
    } else {
        return &vc->second;
    }
}

Epoch VectorClock::find_or(ThreadID thread_id, Epoch fallback) {
    Epoch* epoch_ptr = this->find(thread_id);

    if(epoch_ptr == NULL) {
        return fallback;
    } else {
        return *epoch_ptr;
    }
}

std::vector<ThreadEpochPair> VectorClock::find_all() {
    std::vector<ThreadEpochPair> pairs;

    for(const auto &[thread_id, epoch] : this->_vector_clock) {
        pairs.push_back(ThreadEpochPair { thread_id, epoch });
    }

    return pairs;
}

void VectorClock::set(ThreadID thread_id, Epoch epoch) {
    this->_vector_clock[thread_id] = epoch;
}

/**
 * Loop through all the keys of each vc, compare epoch with the other, take maximum.
 */
VectorClock VectorClock::merge(VectorClock vector_clock) {
    VectorClock new_vector_clock;

    for(const auto &[thread_id, epoch] : this->_vector_clock) {
        new_vector_clock.set(thread_id, epoch);
    }

    for(const ThreadEpochPair thread_epoch_pair : vector_clock.find_all()) {
        Epoch* current_value = new_vector_clock.find(thread_epoch_pair.thread_id);
        if(current_value == NULL || *current_value < thread_epoch_pair.epoch) {
            new_vector_clock.set(thread_epoch_pair.thread_id, thread_epoch_pair.epoch);
        }
    }

    return new_vector_clock;
}

void VectorClock::increment(ThreadID thread_id) {
    Epoch* old_value = this->find(thread_id);
    if(old_value == NULL) {
        this->set(thread_id, 1);
    } else {
        this->set(thread_id, *old_value + 1);
    }
}