#include <vector>
#include <set>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <stack>
#include "detector.hpp"
#include "vectorclock.hpp"
#include "pwrdetector.hpp"
#include "undead.hpp"

/**
 * Optimized PWR + Undead
 */
class PWRUNDEADDetector : public Detector {
    private:
        PWRDetector* pwrDetector;
        UNDEADDetector* undeadDetector;

        struct LockDependency {
            ThreadID id;
            ResourceName resource_name;
            std::set<ResourceName> lockset;
        };

        struct LockDependencyVectorClocks {
            LockDependency lock_dependency;
            std::vector<VectorClock> vectorclocks;

            bool operator==(const LockDependencyVectorClocks& a) const {
                return lock_dependency.id == a.lock_dependency.id && lock_dependency.resource_name == a.lock_dependency.resource_name && lock_dependency.lockset == a.lock_dependency.lockset;
            }

            bool operator<(const LockDependencyVectorClocks& a) const {
                if(lock_dependency.id < a.lock_dependency.id) {
                    return true;
                } else if(a.lock_dependency.id != lock_dependency.id) {
                    return false;
                }

                if(lock_dependency.resource_name < a.lock_dependency.resource_name) {
                    return true;
                } else if(a.lock_dependency.resource_name != lock_dependency.resource_name) {
                    return false;
                }

                if(lock_dependency.lockset < a.lock_dependency.lockset) {
                    return true;
                } else if(a.lock_dependency.lockset != lock_dependency.lockset) {
                    return false;
                }
                
                return false;
            }
        };

        struct Thread {
            ThreadID thread_id;
            /**
             * We save all acq vectorclocks that are mapped to (ls, l).
             * We can use a nested hashmap for that --> vectorclocks_collected[ls][l] would return all possible acq(l) VCs for (ls, l).
             */
            std::map<std::set<ResourceName>, std::unordered_map<ResourceName, std::vector<VectorClock>>> vectorclocks_collected;
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

            l_map->second.push_back(*vc);
        }

        bool isOrderedDependencyInChain(const std::vector<LockDependencyVectorClocks>* dependencyChain) {
            for(auto dependencyIter = dependencyChain->begin(); dependencyIter != dependencyChain->end(); ++dependencyIter) {
                if(std::next(dependencyIter) != dependencyChain->end()) {
                    if(std::next(dependencyIter)->lock_dependency.id <= dependencyChain->begin()->lock_dependency.id) {
                        return false;
                    }
                }
            }

            return true;
        }

        bool check_locksets_overlap(std::set<ResourceName> ls1, std::set<ResourceName> ls2) {
            for(ResourceName rn_t1 : ls1) {
                for(ResourceName rn_t2 : ls2) {
                    if(rn_t1 == rn_t2) {
                        return true;
                    }
                }
            }

            return false;
        }

        bool check_lockset_in_lockset(std::set<ResourceName> lockset_small, std::set<ResourceName> lockset_big) {
            for(ResourceName rn_t1 : lockset_small) {
                if(lockset_big.find(rn_t1) == lockset_big.end()) {
                    return false;
                }
            }

            return true;
        }

        bool dependencyChainLocksetsOverlapForAny(const std::vector<LockDependencyVectorClocks>* dependencyChain) {
            for(auto dependencyIter1 = dependencyChain->begin(); dependencyIter1 != dependencyChain->end(); ++dependencyIter1) {
                for(auto dependencyIter2 = dependencyChain->begin(); dependencyIter2 != dependencyChain->end(); ++dependencyIter2) {
                    // Skip duplicates
                    if(dependencyIter1 != dependencyIter2 && check_locksets_overlap(dependencyIter1->lock_dependency.lockset, dependencyIter2->lock_dependency.lockset)) {
                        return true;
                    }
                }
            }

            return false;
        }

        bool dependencyChainLockInLocksetsCycle(const std::vector<LockDependencyVectorClocks>* dependencyChain) {
            for(auto dependencyIter = dependencyChain->begin(); dependencyIter != dependencyChain->end(); ++dependencyIter) {
                if(std::next(dependencyIter) == dependencyChain->end()) {
                    // We are in last element of vector, check with first (LD-3)
                    if(dependencyChain->begin()->lock_dependency.lockset.find(dependencyIter->lock_dependency.resource_name) == dependencyChain->begin()->lock_dependency.lockset.end()) {
                        return false;
                    }
                } else {
                    // LD-2
                    //if(!check_lockset_in_lockset(dependencyIter->lockset, std::next(dependencyIter)->lockset)) {
                    if(std::next(dependencyIter)->lock_dependency.lockset.find(dependencyIter->lock_dependency.resource_name) == std::next(dependencyIter)->lock_dependency.lockset.end()) {
                        return false;
                    }
                }
            }

            return true;
        }

        bool vector_clocks_not_less_than(const std::vector<LockDependencyVectorClocks>* dependencyChain) {
            for(auto dependencyIter1 = dependencyChain->begin(); dependencyIter1 != dependencyChain->end(); ++dependencyIter1) {
                for(auto dependencyIter2 = dependencyChain->begin(); dependencyIter2 != dependencyChain->end(); ++dependencyIter2) {
                    if(dependencyIter1 == dependencyIter2) {
                        continue;
                    }

                    for(auto vc1 = dependencyIter1->vectorclocks.begin(); vc1 != dependencyIter1->vectorclocks.end(); ++vc1) {
                        for(auto vc2 = dependencyIter2->vectorclocks.begin(); vc2 != dependencyIter2->vectorclocks.end(); ++vc2) {
                            if(vc1 == vc2) continue;
                            
                            bool one_strictly_smaller = false;

                            for(const auto &[thread_id, value]: vc1->_vector_clock) {
                                auto id_val_vc2 = vc2->_vector_clock.find(thread_id);
                                if(id_val_vc2 != vc2->_vector_clock.end()) {
                                    if(value < id_val_vc2->second) {
                                        one_strictly_smaller = true;
                                    }
                                }
                            }

                            for(const auto &[thread_id, value]: vc2->_vector_clock) {
                                auto id_val_vc1 = vc1->_vector_clock.find(thread_id);
                                if(id_val_vc1 == vc1->_vector_clock.end() && value > 0) {
                                    one_strictly_smaller = true;
                                }
                            }

                            if(!one_strictly_smaller) {
                                return false;
                            }
                        }
                    }
                }
            }

            return true;
        }

        void find_cycles() {
            std::vector<LockDependencyVectorClocks> allDependencies = {};

            for(auto &t: threads) {
                for(auto &ls: t.second.vectorclocks_collected) {
                    for(auto &l: ls.second) {
                        allDependencies.push_back(LockDependencyVectorClocks {
                            LockDependency {
                                t.second.thread_id,
                                l.first,
                                ls.first
                            },
                            l.second
                        });
                    }
                }
            }

            std::sort(allDependencies.begin(), allDependencies.end());

            for(int n = 2; n <= std::min(threads.size(), allDependencies.size()); n++) {
                // D_i
                std::set<std::vector<LockDependencyVectorClocks>> permutations = {};

                do {
                    permutations.emplace(allDependencies.begin(), allDependencies.begin() + n);
                } while(std::next_permutation(allDependencies.begin(), allDependencies.end()));

                for(auto &dependencyChain: permutations) {
                    if(!isOrderedDependencyInChain(&dependencyChain) ||
                        dependencyChainLocksetsOverlapForAny(&dependencyChain) ||
                        !dependencyChainLockInLocksetsCycle(&dependencyChain) ||
                        !vector_clocks_not_less_than(&dependencyChain)
                    ) continue;

                    if(this->lockframe != NULL) {
                        this->lockframe->report_race(DataRace{ dependencyChain.back().lock_dependency.resource_name, 0, dependencyChain.front().lock_dependency.id, dependencyChain.back().lock_dependency.id });
                    }
                }
            }
        }

    public:
        PWRUNDEADDetector() {
            this->pwrDetector = new PWRDetector();
            this->undeadDetector = new UNDEADDetector();
        }

        void read_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            this->pwrDetector->read_event(thread_id, trace_position, resource_name);
        }

        void write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            this->pwrDetector->write_event(thread_id, trace_position, resource_name);
        }

        void acquire_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            this->pwrDetector->acquire_event(thread_id, trace_position, resource_name);

            auto previous_ls = this->undeadDetector->get_thread(thread_id)->lockset;
            this->undeadDetector->acquire_event(thread_id, trace_position, resource_name);

            insert_vectorclock_into_thread(
                get_thread(thread_id),
                &this->pwrDetector->get_thread(thread_id)->vector_clock,
                &previous_ls,
                resource_name
            );
        }

        void release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            this->pwrDetector->release_event(thread_id, trace_position, resource_name);
            this->undeadDetector->release_event(thread_id, trace_position, resource_name);
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