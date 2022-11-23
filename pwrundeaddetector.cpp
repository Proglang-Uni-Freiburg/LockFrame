#include <vector>
#include <set>
#include <deque>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <memory>
#include <stack>
#include <chrono>
#include "detector.hpp"
#include "vectorclock.hpp"
#include "pwrdetector.hpp"

/**
 * Optimized PWR + Undead
 */
class PWRUNDEADDetector : public Detector {
    private:
        #ifdef PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT
            const size_t VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT = PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT;
        #else
            const size_t VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT = 5;
        #endif

        #ifdef COLLECT_STATISTICS
            size_t undead_size_of_all_locksets_count = 0;
            size_t pwrundead_size_of_all_locksets_count = 0;
        #endif

        PWRDetector* pwrDetector;

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
            std::set<ResourceName> lockset = {};
            std::map<std::set<ResourceName>, std::unordered_map<ResourceName, std::deque<VectorClock>>> vectorclocks_collected;
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
            #ifdef COLLECT_STATISTICS
                pwrundead_size_of_all_locksets_count += ls->size();
            #endif

            auto ls_map = thread->vectorclocks_collected.find(*ls);
            if(ls_map == thread->vectorclocks_collected.end()) {
                ls_map = thread->vectorclocks_collected.insert({*ls, {}}).first;
            }

            auto l_map = ls_map->second.find(l);
            if(l_map == ls_map->second.end()) {
                l_map = ls_map->second.insert({l, {}}).first;

                #ifdef COLLECT_STATISTICS
                    undead_size_of_all_locksets_count += ls->size();
                #endif
            }

            if(l_map->second.size() >= this->VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT) {
                l_map->second.pop_front();
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
                                *thread_id,
                                l.first,
                                &vc,
                                &d.first
                            });
                            dfs(&chain_stack, visiting, &is_traversed, &thread_ids);
                            chain_stack.pop_back();
                        }
                    }
                }
            }
        }

    public:
        PWRUNDEADDetector() {
            this->pwrDetector = new PWRDetector();
        }

        void read_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            this->pwrDetector->read_event(thread_id, trace_position, resource_name);
        }

        void write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            this->pwrDetector->write_event(thread_id, trace_position, resource_name);
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

            thread->lockset.insert(resource_name);
        }

        void release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);

            this->pwrDetector->release_event(thread_id, trace_position, resource_name);
            thread->lockset.erase(resource_name);
        }

        void fork_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            Thread* thread = get_thread(thread_id);
            Thread* target_thread = get_thread(target_thread_id);
            this->pwrDetector->fork_event(thread_id, trace_position, target_thread_id);
        }

        void join_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            Thread* thread = get_thread(thread_id);
            Thread* target_thread = get_thread(target_thread_id);
            this->pwrDetector->join_event(thread_id, trace_position, target_thread_id);
        }

        void notify_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            this->pwrDetector->notify_event(thread_id, trace_position, resource_name);
        }

        void wait_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);
            this->pwrDetector->wait_event(thread_id, trace_position, resource_name);
        }

        void get_races() {
            #ifdef COLLECT_STATISTICS
                size_t undead_dependency_count = 0;
                std::vector<size_t> undead_dependency_per_thread_count = {};
                size_t pwrundead_dependency_count = 0;
                std::vector<size_t> pwrundead_dependency_per_thread_count = {};

                for(auto &[thread_id, thread]: threads) {
                    size_t dep_counter = 0;
                    size_t vc_dep_counter = 0;
                    for(auto &d: thread.vectorclocks_collected) {
                        for(auto &l: d.second) {
                            dep_counter += 1;
                            for(auto &vc: l.second) {
                                vc_dep_counter += 1;
                            }
                        }
                    }

                    undead_dependency_count += dep_counter;
                    undead_dependency_per_thread_count.push_back(dep_counter);
                    pwrundead_dependency_count += vc_dep_counter;
                    pwrundead_dependency_per_thread_count.push_back(vc_dep_counter);
                }

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

                printf("UNDEAD size of all locksets: %d\n", undead_size_of_all_locksets_count);
                printf("PWRUNDEAD size of all locksets: %d\n", pwrundead_size_of_all_locksets_count);
            #endif

            #ifdef COLLECT_STATISTICS
                auto start = std::chrono::steady_clock::now();
            #endif

            find_cycles();

            #ifdef COLLECT_STATISTICS
                auto end = std::chrono::steady_clock::now();

                printf("Phase 2 elapsed time in milliseconds: %d\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
            #endif
        }
};