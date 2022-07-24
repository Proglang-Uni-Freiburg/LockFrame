#include "vectorclock.hpp"

VectorClock::VectorClock() {}
VectorClock::VectorClock(ThreadID increment_thread_id) {
    this->set(increment_thread_id, 1);
}

VectorClockValue VectorClock::find(ThreadID thread_id) {
    auto vc = this->_vector_clock.find(thread_id);

    if(vc == this->_vector_clock.end()) {
        return 0;
    } else {
        return vc->second;
    }
}

std::vector<Epoch> VectorClock::find_all() {
    std::vector<Epoch> pairs;

    for(const auto &[thread_id, value] : this->_vector_clock) {
        pairs.push_back(Epoch { thread_id, value });
    }

    return pairs;
}

void VectorClock::set(ThreadID thread_id, VectorClockValue value) {
    this->_vector_clock[thread_id] = value;
}

/**
 * Loop through all the keys of each vc, compare epoch with the other, take maximum.
 */
VectorClock VectorClock::merge(VectorClock vector_clock) {
    VectorClock new_vector_clock;

    for(const auto &[thread_id, value] : this->_vector_clock) {
        new_vector_clock.set(thread_id, value);
    }

    for(const Epoch epoch : vector_clock.find_all()) {
        VectorClockValue current_value = new_vector_clock.find(epoch.thread_id);
        if(current_value < epoch.value) {
            new_vector_clock.set(epoch.thread_id, epoch.value);
        }
    }

    return new_vector_clock;
}

void VectorClock::merge_into(VectorClock* vector_clock) {
    for(const auto &[thread_id, value]: vector_clock->_vector_clock) {
        auto vc = _vector_clock.find(thread_id);
        if(vc == _vector_clock.end()) {
            _vector_clock[thread_id] = value;
        } else if(vc->second < value) {
            vc->second = value;
        }
    }
}

void VectorClock::increment(ThreadID thread_id) {
    VectorClockValue old_value = this->find(thread_id);
    if(old_value == 0) {
        this->set(thread_id, 1);
    } else {
        this->set(thread_id, old_value + 1);
    }
}