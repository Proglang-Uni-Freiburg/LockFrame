#include <vector>
#include <set>
#include <deque>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <memory>
#include <stack>
#include "detector.hpp"
#include "vectorclock.hpp"
#include "debug/pwr_no_hist_optimized.hpp"

/**
 * Optimized PWR + Undead + Idea 2 @ release
 */
class PWRUNDEADDetectorExtraEdges : public Detector {
    private:
        #ifdef PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT
            const size_t VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT = PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT;
        #else
            const size_t VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT = 5;
        #endif
        PWRNoHistOptimized* pwrDetector;

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
            auto ls_map = thread->vectorclocks_collected.find(*ls);
            if(ls_map == thread->vectorclocks_collected.end()) {
                ls_map = thread->vectorclocks_collected.insert({*ls, {}}).first;
            }

            auto l_map = ls_map->second.find(l);
            if(l_map == ls_map->second.end()) {
                l_map = ls_map->second.insert({l, {}}).first;
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

            int counter_all = 0;
            for(auto thread_id = thread_ids.cbegin(); thread_id != thread_ids.cend(); ++thread_id) {
                Thread* thread = get_thread(*thread_id);
                if(thread->vectorclocks_collected.empty()) continue;

                for(auto &d: thread->vectorclocks_collected) {
                    for(auto &l: d.second) {
                        for(auto &vc: l.second) {
                            counter_all += 1;
                        }
                    }
                }
            }

            int visiting;
            std::vector<LockDependency> chain_stack = {};
            int index = 0;
            for(auto thread_id = thread_ids.cbegin(); thread_id != thread_ids.cend(); ++thread_id) {
                Thread* thread = get_thread(*thread_id);
                if(thread->vectorclocks_collected.empty()) continue;
                visiting = *thread_id;

                for(auto &d: thread->vectorclocks_collected) {
                    for(auto &l: d.second) {
                        for(auto &vc: l.second) {
                            ++index;
                            printf("%d / %d", index, counter_all);
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

    public:
        PWRUNDEADDetector2() {
            this->pwrDetector = new PWRNoHistOptimized();
        }

        void read_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            this->pwrDetector->read_event(thread_id, trace_position, resource_name);
        }

        void write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
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

        int counter = 0;

        void release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            Thread* thread = get_thread(thread_id);

            auto acquire_vc = this->pwrDetector->get_resource(thread_id)->last_acquire_vc;
            auto release_vc = this->pwrDetector->get_thread(thread_id)->vector_clock;

            this->pwrDetector->release_event(thread_id, trace_position, resource_name);

            for(auto &[rn, pairs]: this->pwrDetector->get_thread(thread_id)->history) {
                if(resource_name == rn) continue;

                // Check if edge already exists
                std::set<ResourceName> ls = {resource_name};
                auto ls_iter = thread->vectorclocks_collected.find(ls);
                if(ls_iter != thread->vectorclocks_collected.end()) {
                    if(ls_iter->second.find(rn) != ls_iter->second.end()) {
                        continue;
                    }
                }

                for(auto &vc_pair: pairs) {
                    if(vc_pair.get()->rel_vc.less_than(&release_vc) && !(vc_pair.get()->acq_vc.less_than(&acquire_vc))) {
                        counter += 1;
                        printf("ADD EXTRA EDGE %d -> %d -- %d\n", resource_name, rn, counter);
                        insert_vectorclock_into_thread(
                            thread,
                            &this->pwrDetector->get_thread(thread_id)->vector_clock,
                            &ls,
                            rn
                        );
                        break;
                    }
                }
            }

            thread->lockset.erase(resource_name);
        }

        void fork_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            this->pwrDetector->fork_event(thread_id, trace_position, target_thread_id);
        }

        void join_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
            this->pwrDetector->join_event(thread_id, trace_position, target_thread_id);
        }

        void notify_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            this->pwrDetector->notify_event(thread_id, trace_position, resource_name);
        }

        void wait_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            this->pwrDetector->wait_event(thread_id, trace_position, resource_name);
        }

        void get_races() {
            find_cycles();
        }
};