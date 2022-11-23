#include <vector>
#include <set>
#include <deque>
#include <utility>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <memory>
#include <stack>
#include "detector.hpp"
#include "vectorclock.hpp"
#include "debug/pwr_for_undead.hpp"

#if COLLECT_STATISTICS
    #include <chrono>
#endif

/**
 * Optimized PWR + Undead + Idea 2 @ sync
 */
class PWRUNDEADDetectorExtraEdgesEarly : public Detector {
    private:
        #ifdef PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT
            const size_t VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT = PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT;
        #else
            const size_t VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT = 5;
        #endif

        #if COLLECT_STATISTICS
            bool processing_extra_edges = false;

            size_t vc_limit_exceeded_counter = 0;
            size_t read_events_counter = 0;
            size_t write_events_counter = 0;
            size_t acquire_events_counter = 0;
            size_t release_events_counter = 0;
            size_t fork_events_counter = 0;
            size_t join_events_counter = 0;
            size_t notify_events_counter = 0;
            size_t wait_events_counter = 0;

            std::unordered_map<std::string, bool> vc_limit_exceeded = {};
            std::unordered_map<ResourceName, bool> locks = {};
        #endif

        PWRForUndeadDetector* pwrDetector;

        struct LockDependency {
            ThreadID id;
            ResourceName resource_name;
            VectorClock* vector_clock;
            const std::set<ResourceName>* lockset;
        };

        struct Thread {
            ThreadID thread_id;
            /**
             * We save all acq vectorclocks that are mapped to (ls, l).
             * We can use a nested hashmap for that --> vectorclocks_collected[ls][l] would return all possible acq(l) VCs for (ls, l).
             */
            //std::unordered_map<ResourceName, VectorClock> lockset_acquire = {};
            std::vector<std::pair<ResourceName, VectorClock>> lockset_acquire = {};
            std::set<ResourceName> lockset = {};
            std::map<std::set<ResourceName>, std::unordered_map<ResourceName, std::deque<VectorClock>>> vectorclocks_collected;

            #if COLLECT_STATISTICS
                size_t undead_edges_counter = 0;
                size_t pwr_edges_counter = 0;
                size_t extra_edges_counter = 0;

                std::unordered_map<std::string, bool> undead_locks = {};
            #endif
        };

        std::unordered_map<ThreadID, Thread> threads = {};

        Thread* get_thread(ThreadID thread_id) {
            auto current_thread_ptr = threads.find(thread_id);
            if(current_thread_ptr == threads.end()) {
                return &threads.insert({
                    thread_id,
                    Thread {
                        thread_id,
                        {}
                    }
                }).first->second;
            } else {
                return &current_thread_ptr->second;
            }
        }

        void insert_vectorclock_into_thread(Thread* thread, VectorClock* vc, std::set<ResourceName>* ls, ResourceName l) {
            auto ls_map = thread->vectorclocks_collected.find(*ls);
            if(ls_map == thread->vectorclocks_collected.end()) {
                ls_map = thread->vectorclocks_collected.insert({*ls, {}}).first;
            }

            auto l_map = ls_map->second.find(l);
            if(l_map == ls_map->second.end()) {
                l_map = ls_map->second.insert({l, {}}).first;
            }

            #if COLLECT_STATISTICS
                if(!this->processing_extra_edges) {
                    std::string key = std::to_string(l);
                    for(auto &lock: *ls) {
                        key += "-";
                        key += std::to_string(lock);
                    }

                    if(thread->undead_locks.find(key) == thread->undead_locks.end()) {
                        thread->undead_edges_counter += 1;
                        thread->undead_locks[key] = true;
                    }

                    thread->pwr_edges_counter += 1;
                }
            #endif

            if(l_map->second.size() >= this->VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT) {
                l_map->second.pop_front();

                #if COLLECT_STATISTICS
                    this->vc_limit_exceeded_counter += 1;

                    std::string key = std::to_string(l);
                    for(auto &lock: *ls) {
                        key += "-";
                        key += std::to_string(lock);
                    }

                    this->vc_limit_exceeded[key] = true;
                #endif
            }
            l_map->second.push_back(*vc);
        }

        bool isCycleChain(std::vector<LockDependency>* chain_stack, LockDependency* dependency) {
            for(auto &chain_dep_ls: *chain_stack->front().lockset) {
                if(chain_dep_ls == dependency->resource_name) return true;
            }
            return false;
        }

        bool isChain(std::vector<LockDependency>* chain_stack, LockDependency* dependency) {
            for(auto &chain_dep: *chain_stack) {
                // Check if (LD-3) l_n in ls_1
                if(chain_dep.resource_name == dependency->resource_name) return false;
                // Check if (LD-1) LS(ls_i) cap LS(ls_j)
                for(auto &chain_dep_ls: *chain_dep.lockset) {
                    for(auto &dependency_ls: *dependency->lockset) {
                        if(chain_dep_ls == dependency_ls) return false;
                    }
                }
            }
            // Check if (LD-2) l_i in ls_i+1 for i=1,...,n-1
            for(auto &dependency_ls: *dependency->lockset) {
                if(chain_stack->back().resource_name == dependency_ls) return true;
            }
            return false;
        }

        bool isChainVC(std::vector<LockDependency>* chain_stack, LockDependency* dependency) {
            for(auto &chain_dep: *chain_stack) {
                // Check if (LD-4) l_i_VC || l_j_VC for i != j
                if(chain_dep.vector_clock->less_than(dependency->vector_clock) || dependency->vector_clock->less_than(chain_dep.vector_clock)) {
                    return false;
                }
            }

            return true;
        }

        void dfs(std::vector<LockDependency>* chain_stack, int visiting_thread_id, std::unordered_map<ThreadID, bool>* is_traversed, std::set<ThreadID>* thread_ids) {
            for(auto thread_id = thread_ids->cbegin(); thread_id != thread_ids->cend(); ++thread_id) {
                if(*thread_id <= visiting_thread_id) continue;
                Thread* thread = get_thread(*thread_id);
                if(thread->vectorclocks_collected.empty()) continue;

                if(!is_traversed->find(*thread_id)->second) {
                    for(auto &d: thread->vectorclocks_collected) {
                        for(auto &l: d.second) {
                            bool is_first = true;
                            bool is_cycle_chain = false;
                            for(auto &vc: l.second) {
                                LockDependency dependency = LockDependency {
                                    *thread_id,
                                    l.first,
                                    &vc,
                                    &d.first
                                };

                                // If it's not a valid chain for conditions LD-1 to LD-3, we don't have to check the other VC deps
                                if(is_first) {
                                    if(!isChain(chain_stack, &dependency)) {
                                        break;
                                    }
                                    is_cycle_chain = isCycleChain(chain_stack, &dependency);
                                    is_first = false;
                                }

                                // Only check for LD-4, we already checked for the previous ones
                                if(isChainVC(chain_stack, &dependency)) {
                                    if(is_cycle_chain) {
                                        this->lockframe->report_race(DataRace{ dependency.resource_name, 0, chain_stack->front().id, dependency.id });
                                    } else {
                                        (*is_traversed)[*thread_id] = true;
                                        chain_stack->push_back(dependency);
                                        dfs(chain_stack, visiting_thread_id, is_traversed, thread_ids);
                                        chain_stack->pop_back();
                                        (*is_traversed)[*thread_id] = false;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        void find_cycles() {
            std::set<ThreadID> thread_ids = {};
            std::unordered_map<ThreadID, bool> is_traversed = {};
            for(auto const& thread: threads) {
                thread_ids.insert(thread.first);
                is_traversed[thread.first] = false;
            }

            int visiting;
            std::vector<LockDependency> chain_stack = {};
            for(auto thread_id = thread_ids.cbegin(); thread_id != thread_ids.cend(); ++thread_id) {
                Thread* thread = get_thread(*thread_id);
                if(thread->vectorclocks_collected.empty()) continue;
                visiting = *thread_id;

                for(auto &d: thread->vectorclocks_collected) {
                    for(auto &l: d.second) {
                        for(auto &vc: l.second) {
                            is_traversed[*thread_id] = true;
                            chain_stack.push_back(LockDependency {
                                LockDependency {
                                    *thread_id,
                                    l.first,
                                    &vc,
                                    &d.first
                                }
                            });
                            dfs(&chain_stack, visiting, &is_traversed, &thread_ids);
                            chain_stack.pop_back();
                        }
                    }
                }
            }
        }

        void check_and_add_extra_edge(Thread* thread, ResourceName resource_name, VectorClock* curr_vc, VectorClock* acq, VectorClock* rel) {
            if(rel->less_than(curr_vc)) {
                std::vector<ResourceName> M_ls_vec = {};
                for(auto &[lock_rn, lock_acq]: thread->lockset_acquire) {
                    if(lock_rn == resource_name) continue;

                    if(!acq->less_than(&lock_acq)) {
                        M_ls_vec.push_back(lock_rn);
                    }
                }

                if(M_ls_vec.size() > 0) {
                    std::set<ResourceName> M_ls(M_ls_vec.begin(), M_ls_vec.end());
                    for(size_t curr_index = M_ls.size(); curr_index > 0; --curr_index) {
                        insert_vectorclock_into_thread(
                            thread,
                            curr_vc,
                            &M_ls,
                            resource_name
                        );
                        #if COLLECT_STATISTICS
                            thread->extra_edges_counter += 1;
                        #endif
                        M_ls.erase(M_ls_vec.at(curr_index - 1));
                    }
                }
            }
        }

        void add_extra_edges(Thread* thread) {
            #if COLLECT_STATISTICS
                this->processing_extra_edges = true;
            #endif

            auto pwr_thread = this->pwrDetector->get_thread(thread->thread_id);
            for(auto &[rn, pairs]: pwr_thread->history) {
                for(auto &vc_pair: pairs) {
                    check_and_add_extra_edge(thread, rn, &pwr_thread->vector_clock, &vc_pair.get()->acq_vc, &vc_pair.get()->rel_vc);
                }
            }
            // Check read-only
            for(auto &[rn, vc_pair]: pwr_thread->history_last_read_only) {
                check_and_add_extra_edge(thread, rn, &pwr_thread->vector_clock, &vc_pair.get()->acq_vc, &vc_pair.get()->rel_vc);
            }

            #if COLLECT_STATISTICS
                this->processing_extra_edges = false;
            #endif
        }

    public:
        PWRUNDEADDetector3() {
            this->pwrDetector = new PWRForUndeadDetector();
        }

        void read_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);

            this->pwrDetector->read_event(thread_id, trace_position, resource_name);

            add_extra_edges(thread);

            #if COLLECT_STATISTICS
                this->read_events_counter += 1;
            #endif
        }

        void write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            get_thread(thread_id);

            this->pwrDetector->write_event(thread_id, trace_position, resource_name);

            #if COLLECT_STATISTICS
                this->write_events_counter += 1;
            #endif
        }

        void acquire_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            this->pwrDetector->acquire_event(thread_id, trace_position, resource_name);

            insert_vectorclock_into_thread(
                thread,
                &this->pwrDetector->get_thread(thread_id)->vector_clock,
                &thread->lockset,
                resource_name
            );

            auto result = thread->lockset.insert(resource_name);
            if(result.second) {
                thread->lockset_acquire.push_back(std::make_pair(resource_name, pwrDetector->get_resource(resource_name)->last_acquire_vc));
            }

            #if COLLECT_STATISTICS
                this->acquire_events_counter += 1;

                if(this->locks.find(resource_name) == this->locks.end()) {
                    this->locks[resource_name] = true;
                }
            #endif
        }

        void release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);

            this->pwrDetector->release_event(thread_id, trace_position, resource_name);
            thread->lockset.erase(resource_name);
            for(auto lockset_entry = thread->lockset_acquire.rbegin(); lockset_entry != thread->lockset_acquire.rend(); ++lockset_entry) {
                if(resource_name == lockset_entry->first) {
                    thread->lockset_acquire.erase(std::next(lockset_entry).base());
                    break;
                }
            }

            #if COLLECT_STATISTICS
                this->release_events_counter += 1;
            #endif
        }

        void fork_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            get_thread(thread_id);

            this->pwrDetector->fork_event(thread_id, trace_position, target_thread_id);

            #if COLLECT_STATISTICS
                this->fork_events_counter += 1;
            #endif
        }

        void join_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            get_thread(thread_id);

            this->pwrDetector->join_event(thread_id, trace_position, target_thread_id);

            add_extra_edges(get_thread(thread_id));

            #if COLLECT_STATISTICS
                this->join_events_counter += 1;
            #endif
        }

        void notify_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            get_thread(thread_id);
            this->pwrDetector->notify_event(thread_id, trace_position, resource_name);

            #if COLLECT_STATISTICS
                this->notify_events_counter += 1;
            #endif
        }

        void wait_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            get_thread(thread_id);
            this->pwrDetector->wait_event(thread_id, trace_position, resource_name);

            #if COLLECT_STATISTICS
                this->wait_events_counter += 1;
            #endif
        }

        void get_races() {
            #if COLLECT_STATISTICS
                auto start = std::chrono::steady_clock::now();
            #endif

            find_cycles();

            #if COLLECT_STATISTICS
                auto end = std::chrono::steady_clock::now();

                printf("Phase 2 elapsed time in milliseconds: %d\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

                size_t undead_dependency_count = 0;
                std::vector<size_t> undead_dependency_per_thread_count = {};
                size_t pwrundead_dependency_count = 0;
                std::vector<size_t> pwrundead_dependency_per_thread_count = {};
                size_t extra_dependency_count = 0;
                std::vector<size_t> extra_dependency_per_thread_count = {};

                for(auto &[thread_id, thread]: threads) {
                    undead_dependency_count += thread.undead_edges_counter;
                    undead_dependency_per_thread_count.push_back(thread.undead_edges_counter);
                    pwrundead_dependency_count += thread.pwr_edges_counter;
                    pwrundead_dependency_per_thread_count.push_back(thread.pwr_edges_counter);
                    extra_dependency_count += thread.extra_edges_counter;
                    extra_dependency_per_thread_count.push_back(thread.extra_edges_counter);
                }

                printf("Threads: %d\n", this->threads.size());
                printf("UNDEAD dependencies sum: %d\n", undead_dependency_count);
                printf("UNDEAD dependencies per thread:");
                for(auto &counter: undead_dependency_per_thread_count) {
                    printf(" %d", counter);
                }
                printf("\n");

                printf("PWRUNDEAD dependencies sum: %d\n", pwrundead_dependency_count);
                printf("PWRUNDEAD dependencies per thread:");
                for(auto &counter: pwrundead_dependency_per_thread_count) {
                    printf(" %d", counter);
                }
                printf("\n");

                printf("EXTRAEDGES dependencies sum: %d\n", extra_dependency_count);
                printf("EXTRAEDGES dependencies per thread:");
                for(auto &counter: extra_dependency_per_thread_count) {
                    printf(" %d", counter);
                }
                printf("\n");

                printf("reads: %d\n", this->read_events_counter);
                printf("writes: %d\n", this->write_events_counter);
                printf("acquires: %d\n", this->acquire_events_counter);
                printf("releases: %d\n", this->release_events_counter);
                printf("forks: %d\n", this->fork_events_counter);
                printf("joins: %d\n", this->join_events_counter);
                printf("notifies: %d\n", this->notify_events_counter);
                printf("waits: %d\n", this->wait_events_counter);
                printf("locks: %d\n", this->locks.size());

                printf("VC limit exceeded: %d\n", this->vc_limit_exceeded_counter);
                printf("VC limit exceeded by dependency: %d\n", this->vc_limit_exceeded.size());
            #endif
        }
};