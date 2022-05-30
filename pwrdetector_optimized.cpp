#include <vector>
#include <unordered_map>
#include <algorithm>
#include "detector.hpp"
#include "vectorclock.hpp"

#define THREAD_HISTORY_SIZE 5

class PWRDetectorOptimized : public Detector {
    private:
        struct EpochVCPair {
            Epoch epoch;
            VectorClock vector_clock;
        };
        struct EpochLSPair {
            Epoch epoch;
            std::vector<ResourceName> lockset;
        };
        struct Thread {
            ThreadID id;
            // LS(i)
            std::vector<ResourceName> lockset = {};
            // H(y)
            std::unordered_map<ResourceName, std::vector<EpochVCPair>> history = {};
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
        // We don't know about all threads at the beginning, so we have to save a global history in order to load it into a newly spawned thread.
        // This history is still optimized for limited size.
        std::unordered_map<ResourceName, std::vector<EpochVCPair>> global_history = {};

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

                std::unordered_map<ResourceName, std::vector<EpochVCPair>> new_history = {};
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
                return &resources.insert({
                    resource_name,
                    Resource{}
                }).first->second;
            } else {
                return &current_resource_ptr->second;
            }
        }

        // Adds EpochVCPair to thread's history and global history.
        // Limits history size per mutex to THREAD_HISTORY_SIZE (see at top of file).
        void add_to_history(Thread* current_thread, ResourceName resource_name, EpochVCPair epoch_vc_pair) {
            for(auto thread_iter = threads.begin(); thread_iter != threads.end(); ++thread_iter) {
                if(thread_iter->first == current_thread->id) {
                    continue;
                }

                auto current_history = &thread_iter->second.history.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
                if(current_history->size() >= THREAD_HISTORY_SIZE) {
                    current_history->pop_back();
                }
                current_history->insert(current_history->begin(), epoch_vc_pair);
            }

            auto current_global_history = &global_history.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
            if(current_global_history->size() >= THREAD_HISTORY_SIZE) {
                current_global_history->pop_back();
            }
            current_global_history->insert(current_global_history->begin(), epoch_vc_pair);
        }

        /**
         * @brief Prints all current global variable values
         * 
         * @param text 
         * @param thread_id 
         * @param resource_name 
         */
        void debug_print(std::string text, ThreadID thread_id, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            Resource* resource = get_resource(resource_name);

            printf("\n---DEBUG [%s] T<%d> R<%s>---\n", text.c_str(), thread_id, resource_name.c_str());
            
            printf("vc: { ");
            for(auto epoch : thread->vector_clock.find_all()) {
                printf("T%d:%d ", epoch.thread_id, epoch.value);
            }
            printf("}\n");

            printf("lockset: { ");
            for(auto res : thread->lockset) {
                printf("%s", res.c_str());
            }
            printf(" }\n");

            printf("history: {");
            for(auto thread_iter = threads.begin(); thread_iter != threads.end(); ++thread_iter) {
                std::vector<EpochVCPair> current_history = thread_iter->second.history.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
                printf(" T%d: { ", thread_iter->first);
                for(auto pair : current_history) {
                    printf("%d#%d, { ", pair.epoch.thread_id, pair.epoch.value);
                    for(auto epoch : pair.vector_clock.find_all()) {
                        printf("%d@%d ", epoch.thread_id, epoch.value);
                    }
                    printf("}");
                }
                printf(" }");
            }
            printf(" }\n");

            printf("rw: { ");
            for(auto pairs : resource->read_write_events) {
                printf("%d#%d { ", pairs.epoch.thread_id, pairs.epoch.value);
                for(auto res : pairs.lockset) {
                    printf("%s", res.c_str());
                }
                printf(" } ");
            }
            printf("}\n");

            printf("acq: ");
                printf("%d#%d", resource->last_acquire.thread_id, resource->last_acquire.value);
            printf("\n");

            printf("l_w: { ");
            if(resource->last_write_occured) {
                for(auto epoch : resource->last_write_vc.find_all()) {
                    printf("T%d:%d ", epoch.thread_id, epoch.value);
                }
            }
            printf(" }\n");

            printf("l_wt: %d\n", (resource->last_write_occured) ? resource->last_write_thread : 0);

            printf("l_wls: { ");
            if(resource->last_write_occured) {
                for(auto res : resource->last_write_ls) {
                    printf("%s ", res.c_str());
                }
            }
            printf(" }\n");

            printf("------\n");
        }

        // w3
        void pwr_history_sync(Thread* thread, Resource* resource) {
            for(ResourceName lock : thread->lockset) {
                auto current_history = thread->history.emplace(std::piecewise_construct, std::forward_as_tuple(lock), std::forward_as_tuple()).first->second;

                for(auto epoch_vc_pair_iter = current_history.begin(); epoch_vc_pair_iter != current_history.end();) {
                    if(epoch_vc_pair_iter->vector_clock.find(epoch_vc_pair_iter->epoch.thread_id) < thread->vector_clock.find(epoch_vc_pair_iter->epoch.thread_id)) {
                        epoch_vc_pair_iter = current_history.erase(epoch_vc_pair_iter);
                    } else {
                        if(epoch_vc_pair_iter->epoch.value < thread->vector_clock.find(epoch_vc_pair_iter->epoch.thread_id)) {
                            thread->vector_clock = thread->vector_clock.merge(epoch_vc_pair_iter->vector_clock);
                        }

                        ++epoch_vc_pair_iter;
                    }
                }
            }
        }

        // RW = { (i#Th(i)[i], LS_t(i) } U { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
        void update_read_write_events(Thread* thread, Resource* resource) {
            // (i#Th(i)[i], LS_t(i))
            // Current "timestamp" of the calling thread with its current locks.
            std::vector<EpochLSPair> read_write_events_new = {
                EpochLSPair { Epoch { thread->id, thread->vector_clock.find(thread->id) }, thread->lockset }
            };

            // { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
            // Find everything in RW(x) where thread_2's epoch value is higher than the vector clock of thread_id is set at thread_2.
            for(EpochLSPair rw_pair : resource->read_write_events) {
                if(rw_pair.epoch.value > thread->vector_clock.find(rw_pair.epoch.thread_id)) {
                    read_write_events_new.push_back(rw_pair);
                }
            }

            resource->read_write_events = read_write_events_new;
        }

        bool check_locksets_overlap(std::vector<ResourceName> ls1, std::vector<ResourceName> ls2) {
            for(ResourceName rn_t1 : ls1) {
                for(ResourceName rn_t2 : ls2) {
                    if(rn_t1 == rn_t2) {
                        return true;
                    }
                }
            }

            return false;
        }

        void add_races(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name, std::vector<EpochLSPair> rw_pairs, VectorClock vc, std::vector<ResourceName> ls) {
            // Race check
            // Go through each currently stored read_write_event,
            // check if any lockset doesn't overlap with current one and
            // the other epoch is larger than thread_id's currently stored one for the other thread.
            for(EpochLSPair rw_pair : rw_pairs) {
                if(rw_pair.epoch.value > vc.find(rw_pair.epoch.thread_id)) {
                    if(!check_locksets_overlap(ls, rw_pair.lockset)) {
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
                   !check_locksets_overlap(resource->last_write_ls, thread->lockset)) {
                    add_races(
                        thread_id,
                        trace_position,
                        resource_name,
                        resource->read_write_events,
                        thread->vector_clock,
                        thread->lockset
                    );
                }

                // Th(i) = Th(i) |_| L_w(x)
                // "Compare values per thread, set to max ==> merge operation"
                thread->vector_clock = thread->vector_clock.merge(resource->last_write_vc);
            }

            add_races(
                thread_id,
                trace_position,
                resource_name,
                resource->read_write_events,
                thread->vector_clock,
                thread->lockset
            );

            pwr_history_sync(thread, resource);

            update_read_write_events(thread, resource);

            thread->vector_clock.increment(thread->id);

            debug_print("read", thread_id, resource_name);
        }

        void write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            Resource* resource = get_resource(resource_name);

            pwr_history_sync(thread, resource);

            add_races(
                thread_id,
                trace_position,
                resource_name,
                resource->read_write_events,
                thread->vector_clock,
                thread->lockset
            );

            update_read_write_events(thread, resource);

            resource->last_write_vc = thread->vector_clock;
            resource->last_write_thread = thread_id;
            resource->last_write_ls = thread->lockset;
            resource->last_write_occured = true;

            thread->vector_clock.increment(thread->id);

            debug_print("write", thread_id, resource_name);
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

            debug_print("acquire", thread_id, resource_name);
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
            add_to_history(
                thread,
                resource_name,
                EpochVCPair{
                    resource->last_acquire,
                    thread->vector_clock
                }
            );

            thread->vector_clock.increment(thread->id);

            debug_print("release", thread_id, resource_name);
        }

        void fork_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            Thread* thread = get_thread(thread_id);
            Thread* target_thread = get_thread(target_thread_id);

            target_thread->vector_clock = thread->vector_clock;

            thread->vector_clock.increment(thread->id);
        }

        void join_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            Thread* thread = get_thread(thread_id);
            Thread* target_thread = get_thread(target_thread_id);

            target_thread->vector_clock = thread->vector_clock.merge(target_thread->vector_clock);

            thread->vector_clock.increment(thread->id);
        }

        void get_races() {
        }
};