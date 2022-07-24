#include <vector>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include "../detector.hpp"
#include "../vectorclock.hpp"

#define THREAD_HISTORY_SIZE 5

/**
 * Optimizations:
 *      - Paper
 *      - Use pointers
 *      - LocalHistRemove
 */
class PWRRemoveAfterSync : public Detector {
    private:
        struct EpochVCPair {
            Epoch epoch;
            VectorClock vector_clock;
        };
        struct EpochLSPair {
            Epoch epoch;
            std::vector<ResourceName> lockset;
            bool is_write;
        };
        struct Thread {
            ThreadID id;
            // LS(i)
            std::vector<ResourceName> lockset = {};
            // H(y)
            std::unordered_map<ResourceName, std::deque<EpochVCPair>> history = {};
            // Th(i)
            VectorClock vector_clock = {};
        };
        struct Resource {
            // RW(x)
            std::vector<EpochLSPair> read_write_events = {};
            // Acq(y)
            Epoch last_acquire = {};
            // L_w(x)
            VectorClock last_write_vc = {};
            // L_wt(x)
            ThreadID last_write_thread = {};
            // L_wl(x)
            std::vector<ResourceName> last_write_ls = {};
            // Helper variable, we need to check if write already occured,
            // but defaults stored in last_* variables don't tell us.
            bool last_write_occured = false;
        };

        std::unordered_map<ThreadID, Thread> threads = {};
        std::unordered_map<ResourceName, Resource> resources = {};
        std::unordered_map<ResourceName, VectorClock> notifies = {};
        // We don't know about all threads at the beginning, so we have to save a global history in order to load it into a newly spawned thread.
        // This history is still optimized for limited size.
        std::unordered_map<ResourceName, std::deque<EpochVCPair>> global_history = {};

        /**
         * We have a thread-local history, but other threads need to "know" what happened before they are first encountered,
         * so we copy history from the other thread's
         */
        Thread* get_thread(ThreadID thread_id) {
            auto current_thread_ptr = threads.find(thread_id);
            if(current_thread_ptr == threads.end()) {
                /*std::unordered_map<ResourceName, std::unordered_map<std::string, EpochVCPair>> new_history_without_duplicates = {};
                for(auto thread_iter = threads.begin(); thread_iter != threads.end(); ++thread_iter) {
                    for(auto history: thread_iter->second.history) {
                        auto current_history_map = &new_history_without_duplicates.emplace(std::piecewise_construct, std::forward_as_tuple(history.first), std::forward_as_tuple()).first->second;
                        for(auto history_iter = history.second.begin(); history_iter != history.second.end(); ++history_iter) {
                            auto key_pair = std::to_string(history_iter->epoch.thread_id) + std::string("#") + std::to_string(history_iter->epoch.value);
                            auto current_history_element_ptr = current_history_map->find(key_pair);
                            if(current_history_element_ptr == current_history_map->end()) {
                                current_history_map->insert({ key_pair, *history_iter});
                            }
                        }
                    }
                }

                std::unordered_map<ResourceName, std::vector<EpochVCPair>> new_history = {};
                for(auto new_history_iter: new_history_without_duplicates) {
                    std::vector<EpochVCPair> new_history_resource_vec = {};
                    for(auto new_history_resource_iter: new_history_iter.second) {
                        new_history_resource_vec.push_back(new_history_resource_iter.second);
                    }
                    new_history[new_history_iter.first] = new_history_resource_vec;
                }*/

                std::unordered_map<ResourceName, std::deque<EpochVCPair>> new_history = {};
                for(auto history_pair : global_history) {
                    new_history[history_pair.first] = history_pair.second;
                }

                return &threads.insert({
                    thread_id,
                    Thread {
                        thread_id,
                        {},
                        new_history,
                        VectorClock(thread_id)
                    }
                }).first->second;
            } else {
                return &current_thread_ptr->second;
            }
        }

        Resource* get_resource(ResourceName resource_name) {
            auto current_resource_ptr = resources.find(resource_name);
            if(current_resource_ptr == resources.end()) {
                /*return &resources.insert({
                    resource_name,
                    Resource{}
                }).first->second;*/
                return &resources.insert(std::make_pair(resource_name, Resource{})).first->second;
            } else {
                return &current_resource_ptr->second;
            }
        }

        // w3
        void pwr_history_sync(Thread* thread, Resource* resource) {
            for(ResourceName lock : thread->lockset) {
                auto current_history_iter = thread->history.find(lock);
                if(current_history_iter == thread->history.end()) continue;
                auto current_history = &current_history_iter->second;

                for(auto epoch_vc_pair_iter = current_history->begin(); epoch_vc_pair_iter != current_history->end();) {
                    if(epoch_vc_pair_iter->vector_clock.find(epoch_vc_pair_iter->epoch.thread_id) < thread->vector_clock.find(epoch_vc_pair_iter->epoch.thread_id)) {
                        epoch_vc_pair_iter = current_history->erase(epoch_vc_pair_iter);
                    } else {
                        if(epoch_vc_pair_iter->epoch.value < thread->vector_clock.find(epoch_vc_pair_iter->epoch.thread_id)) {
                            thread->vector_clock.merge_into(&epoch_vc_pair_iter->vector_clock);
                            epoch_vc_pair_iter = current_history->erase(epoch_vc_pair_iter);
                        } else {
                            ++epoch_vc_pair_iter;
                        }
                    }
                }
            }
        }

        // RW = { (i#Th(i)[i], LS_t(i) } U { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
        void update_read_write_events(Thread* thread, Resource* resource, bool is_write) {
            // (i#Th(i)[i], LS_t(i))
            // Current "timestamp" of the calling thread with its current locks.

            // { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
            // Find everything in RW(x) where thread_2's epoch value is higher than the vector clock of thread_id is set at thread_2.
            for(auto rw_pair_iter = resource->read_write_events.begin(); rw_pair_iter != resource->read_write_events.end();) {
                if(rw_pair_iter->epoch.value <= thread->vector_clock.find(rw_pair_iter->epoch.thread_id) && !(!is_write && rw_pair_iter->is_write)) {
                    rw_pair_iter = resource->read_write_events.erase(rw_pair_iter);
                } else {
                    ++rw_pair_iter;
                }
            }

            resource->read_write_events.push_back(EpochLSPair { Epoch { thread->id, thread->vector_clock.find(thread->id) }, thread->lockset, is_write });
        }

        bool check_locksets_overlap(std::vector<ResourceName> *ls1, std::vector<ResourceName> *ls2) {
            for(ResourceName rn_t1 : *ls1) {
                for(ResourceName rn_t2 : *ls2) {
                    if(rn_t1 == rn_t2) {
                        return true;
                    }
                }
            }

            return false;
        }

        void add_races(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name, std::vector<EpochLSPair> *rw_pairs, VectorClock *vc, std::vector<ResourceName> *ls) {
            // Race check
            // Go through each currently stored read_write_event,
            // check if any lockset doesn't overlap with current one and
            // the other epoch is larger than thread_id's currently stored one for the other thread.
            for(auto &rw_pair : *rw_pairs) {
                if(rw_pair.epoch.value > vc->find(rw_pair.epoch.thread_id)) {
                    if(rw_pair.is_write && !check_locksets_overlap(ls, &rw_pair.lockset)) {
                        report_potential_race(resource_name, trace_position, thread_id, rw_pair.epoch.thread_id);
                    }
                }
            }
        }

        void report_potential_race(ResourceName resource_name, TracePosition trace_position, ThreadID thread_id_1, ThreadID thread_id_2) {
            if(this->lockframe != NULL) {
                this->lockframe->report_race(DataRace{ resource_name, trace_position, thread_id_1, thread_id_2 });
            }
        }

    public:
        void read_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            Resource* resource = get_resource(resource_name);

            if(resource->last_write_occured) {
                // if L_w(x)[j] > Th(i)[j] AND L_wl(x) ∩ LS(i) = {}
                // THERE IS A DISCREPANCY IN THE PAPER VS DISSERTATION HERE!
                // L_w and Th are swapped but then they wouldn't find the WR-Race
                if(resource->last_write_vc.find(resource->last_write_thread) > thread->vector_clock.find(resource->last_write_thread) &&
                   !check_locksets_overlap(&resource->last_write_ls, &thread->lockset)) {
                    add_races(
                        thread_id,
                        trace_position,
                        resource_name,
                        &resource->read_write_events,
                        &thread->vector_clock,
                        &thread->lockset
                    );
                }

                // Th(i) = Th(i) |_| L_w(x)
                // "Compare values per thread, set to max ==> merge operation"
                thread->vector_clock.merge_into(&resource->last_write_vc);
            }

            pwr_history_sync(thread, resource);

            add_races(
                thread_id,
                trace_position,
                resource_name,
                &resource->read_write_events,
                &thread->vector_clock,
                &thread->lockset
            );

            update_read_write_events(thread, resource, false);

            thread->vector_clock.increment(thread->id);
        }

        void write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            Resource* resource = get_resource(resource_name);

            pwr_history_sync(thread, resource);

            add_races(
                thread_id,
                trace_position,
                resource_name,
                &resource->read_write_events,
                &thread->vector_clock,
                &thread->lockset
            );

            update_read_write_events(thread, resource, true);

            resource->last_write_vc = thread->vector_clock;
            resource->last_write_thread = thread_id;
            resource->last_write_ls = thread->lockset;
            resource->last_write_occured = true;

            thread->vector_clock.increment(thread->id);
        }

        void acquire_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            Resource* resource = get_resource(resource_name);

            pwr_history_sync(thread, resource);

            // Add Resource to Lockset
            if(std::find(thread->lockset.begin(), thread->lockset.end(), resource_name) == thread->lockset.end()) {
                thread->lockset.push_back(resource_name);
            }

            // Set acquire History
            resource->last_acquire = Epoch { thread_id, thread->vector_clock.find(thread_id) };
            
            thread->vector_clock.increment(thread->id);
        }

        void release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            Resource* resource = get_resource(resource_name);

            pwr_history_sync(thread, resource);

            // Remove Resource from Lockset
            auto resource_iter = std::find(thread->lockset.begin(), thread->lockset.end(), resource_name);
            if(resource_iter != thread->lockset.end()) {
                thread->lockset.erase(resource_iter);
            }

            // Add to history
            // Adds EpochVCPair to thread's history and global history.
            // Limits history size per mutex to THREAD_HISTORY_SIZE (see at top of file).
            EpochVCPair epoch_vc_pair = EpochVCPair{
                resource->last_acquire,
                thread->vector_clock
            };
            for(auto thread_iter = threads.begin(); thread_iter != threads.end(); ++thread_iter) {
                if(thread_iter->first == thread->id) {
                    continue;
                }

                auto current_history = &thread_iter->second.history.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
                if(current_history->size() >= THREAD_HISTORY_SIZE) {
                    current_history->pop_back();
                }
                current_history->push_front(epoch_vc_pair);
            }

            auto current_global_history = &global_history.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
            if(current_global_history->size() >= THREAD_HISTORY_SIZE) {
                current_global_history->pop_back();
            }
            current_global_history->push_front(epoch_vc_pair);

            thread->vector_clock.increment(thread->id);
        }

        void fork_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            Thread* thread = get_thread(thread_id);
            Thread* target_thread = get_thread(target_thread_id);

            target_thread->vector_clock = thread->vector_clock;
            target_thread->vector_clock.increment(target_thread_id);

            thread->vector_clock.increment(thread->id);
        }

        void join_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            Thread* thread = get_thread(thread_id);
            Thread* target_thread = get_thread(target_thread_id);

            thread->vector_clock.merge_into(&target_thread->vector_clock);

            thread->vector_clock.increment(thread->id);
        }

        void notify_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            
            auto notifies_iter = notifies.find(resource_name);
            if(notifies_iter == notifies.end()) {
                notifies_iter = notifies.insert({ resource_name, VectorClock() }).first;
            }

            notifies_iter->second.merge_into(&thread->vector_clock);
            thread->vector_clock.merge_into(&notifies_iter->second);

            thread->vector_clock.increment(thread_id);
        }

        void wait_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            
            auto notifies_iter = notifies.find(resource_name);
            
            if(notifies_iter == notifies.end()) {
                return;
            }

            thread->vector_clock.merge_into(&notifies_iter->second);
            thread->vector_clock.increment(thread_id);

            notifies[resource_name] = thread->vector_clock;
        }

        void get_races() {}
};