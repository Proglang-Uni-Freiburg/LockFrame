#include "pwrdetector_optimized_4.hpp"

/**
 * Optimizations:
 *      - Paper
 *      - Only merge VC @ read_event when not already merged before
 * 
 *      - pwr_sync extra check if k < V[j] && V[j] < V'[j] then
 *      - Write-NoSync
 *      - Fork-NoSync
 *      - Reads-NoSync
 *      - LocalHistRemove
 *      - LocalHistDontAddReads
 */

        
void PWRDetectorOptimized4::debug_print(std::string text, ThreadID thread_id, ResourceName resource_name) {
    if(!ENABLE_DEBUG_PRINT) {
        return;
    }

    Thread* thread = get_thread(thread_id);
    Resource* resource = get_resource(resource_name);

    printf("\n---DEBUG [%s] T<%d> R<%s>---\n", text.c_str(), thread_id, resource_name);
    
    printf("vc: { ");
    for(auto epoch : thread->vector_clock.find_all()) {
        printf("T%d:%d ", epoch.thread_id, epoch.value);
    }
    printf("}\n");

    printf("lockset: { ");
    for(auto res : thread->lockset) {
        printf("%s", res);
    }
    printf(" }\n");

    printf("history: {");
    for(auto thread_iter = threads.begin(); thread_iter != threads.end(); ++thread_iter) {
        auto hist = &thread_iter->second.history;
        auto hist_res_ptr = hist->find(resource_name);
        printf(" T%d: { ", thread_iter->first);
        if(hist_res_ptr != hist->end()) {
            for(auto pair : hist_res_ptr->second) {
                printf("%d#%d, { ", pair->epoch.thread_id, pair->epoch.value);
                for(auto epoch : pair->vector_clock.find_all()) {
                    printf("%d@%d ", epoch.thread_id, epoch.value);
                }
                printf("}");
            }
        }
        printf(" }");
    }
    printf(" }\n");

    printf("rw: { ");
    for(auto pairs : resource->read_write_events) {
        printf("%d#%d { ", pairs.epoch.thread_id, pairs.epoch.value);
        for(auto res : pairs.lockset) {
            printf("%s", res);
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
            printf("%s ", res);
        }
    }
    printf(" }\n");

    printf("------\n");
}

int cond1Cnt = 0;
int cond2Cnt = 0;
int trace_position_global = 0;
// w3
void PWRDetectorOptimized4::pwr_history_sync(Thread* thread, Resource* resource) {
    for(ResourceName lock : thread->lockset) {
        auto current_history_iter = thread->history.find(lock);
        if(current_history_iter == thread->history.end()) continue;
        auto current_history = &current_history_iter->second;

        for(auto epoch_vc_pair_iter = current_history->begin(); epoch_vc_pair_iter != current_history->end();) {
            auto thread_id_j = epoch_vc_pair_iter->get()->epoch.thread_id;
            auto value_k = epoch_vc_pair_iter->get()->epoch.value;
            auto vc_dash = &epoch_vc_pair_iter->get()->vector_clock;
            auto vc_value_at_j = thread->vector_clock.find(thread_id_j);
            auto vc_dash_value_at_j = vc_dash->find(thread_id_j);
            
            cond1Cnt++;

            if(vc_dash_value_at_j == vc_value_at_j) {
                cond2Cnt++;
            }

            if(vc_dash_value_at_j <= vc_value_at_j) {
                epoch_vc_pair_iter = current_history->erase(epoch_vc_pair_iter);
            } else {
                if(value_k < vc_value_at_j && vc_value_at_j < vc_dash_value_at_j) {
                    thread->vector_clock.merge_into(vc_dash);
                    epoch_vc_pair_iter = current_history->erase(epoch_vc_pair_iter);
                } else {
                    ++epoch_vc_pair_iter;
                }
            }
        }
    }
}

// RW = { (i#Th(i)[i], LS_t(i) } U { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
void PWRDetectorOptimized4::update_read_write_events(Thread* thread, Resource* resource, bool is_write) {
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

bool PWRDetectorOptimized4::check_locksets_overlap(std::vector<ResourceName> *ls1, std::vector<ResourceName> *ls2) {
    for(ResourceName rn_t1 : *ls1) {
        for(ResourceName rn_t2 : *ls2) {
            if(rn_t1 == rn_t2) {
                return true;
            }
        }
    }

    return false;
}
int read_report = 0;
int write_report = 0;

void PWRDetectorOptimized4::add_races(bool is_write, ThreadID thread_id, TracePosition trace_position, ResourceName resource_name, std::vector<EpochLSPair> *rw_pairs, VectorClock *vc, std::vector<ResourceName> *ls) {
    // Race check
    // Go through each currently stored read_write_event,
    // check if any lockset doesn't overlap with current one and
    // the other epoch is larger than thread_id's currently stored one for the other thread.
    for(auto &rw_pair : *rw_pairs) {
        if(rw_pair.epoch.value > vc->find(rw_pair.epoch.thread_id)) {
            if(rw_pair.is_write && !check_locksets_overlap(ls, &rw_pair.lockset)) {
                if(is_write) {
                    write_report += 1;
                } else {
                    read_report += 1;
                }
                report_potential_race(resource_name, trace_position, thread_id, rw_pair.epoch.thread_id);
            }
        }
    }
}

void PWRDetectorOptimized4::report_potential_race(ResourceName resource_name, TracePosition trace_position, ThreadID thread_id_1, ThreadID thread_id_2) {
    if(this->lockframe != NULL) {
        this->lockframe->report_race(DataRace{ resource_name, trace_position, thread_id_1, thread_id_2 });
    }
}

PWRDetectorOptimized4::Thread* PWRDetectorOptimized4::get_thread(ThreadID thread_id) {
    auto current_thread_ptr = threads.find(thread_id);
    if(current_thread_ptr == threads.end()) {
        /**
            * We have a thread-local history, but other threads need to "know" what happened before they are first encountered,
            * so we copy history from the other thread's
            */
        std::unordered_map<ResourceName, std::deque<std::shared_ptr<EpochVCPair>>> new_history = {};
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

PWRDetectorOptimized4::Resource* PWRDetectorOptimized4::get_resource(ResourceName resource_name) {
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

int cntReads = 0;
int cntReadsNoSync = 0;

int reads = 0;
int writes = 0;
int acquires = 0;
int releases = 0;
int notifiesCnt = 0;
int notifywaits = 0;
int forks = 0;
int joins = 0;


void PWRDetectorOptimized4::read_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
    reads += 1;
    trace_position_global = trace_position;
    Thread* thread = get_thread(thread_id);
    Resource* resource = get_resource(resource_name);

    cntReads += 1;

    if(resource->last_write_occured) {
        auto last_read_merge = thread->last_read_merges.find(resource_name);
        bool first_read_after_write = resource->last_write_occured && (last_read_merge == thread->last_read_merges.end() || last_read_merge->second < resource->last_write_occured_at);
        if(first_read_after_write) {
            thread->last_read_merges[resource_name] = resource->last_write_occured_at;
        }
        if(first_read_after_write) {
            // if L_w(x)[j] > Th(i)[j] AND L_wl(x) ∩ LS(i) = {}
            // THERE IS A DISCREPANCY IN THE PAPER VS DISSERTATION HERE!
            // L_w and Th are swapped but then they wouldn't find the WR-Race
            if(resource->last_write_vc.find(resource->last_write_thread) > thread->vector_clock.find(resource->last_write_thread) &&
                !check_locksets_overlap(&resource->last_write_ls, &thread->lockset)) {
                    read_report += 1;
                report_potential_race(resource_name, trace_position, thread_id, resource->last_write_thread);
            }

            // Th(i) = Th(i) |_| L_w(x)
            // "Compare values per thread, set to max ==> merge operation"
            thread->vector_clock.merge_into(&resource->last_write_vc);

            pwr_history_sync(thread, resource);
        } else {
            cntReadsNoSync += 1;
        }
    } else {
        cntReadsNoSync += 1;
    }

    add_races(
        false,
        thread_id,
        trace_position,
        resource_name,
        &resource->read_write_events,
        &thread->vector_clock,
        &thread->lockset
    );

    update_read_write_events(thread, resource, false);

    thread->vector_clock.increment(thread->id);

    debug_print("read", thread_id, resource_name);
}

void PWRDetectorOptimized4::write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
    writes += 1;
    Thread* thread = get_thread(thread_id);
    Resource* resource = get_resource(resource_name);

    add_races(
        true,
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
    resource->last_write_occured_at = trace_position;

    for(auto write_since_acquire_iter = thread->write_since_acquire.begin(); write_since_acquire_iter != thread->write_since_acquire.end(); ++write_since_acquire_iter) {
        *write_since_acquire_iter = true;
    }

    thread->vector_clock.increment(thread->id);

    debug_print("write", thread_id, resource_name);
}

void PWRDetectorOptimized4::acquire_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
    acquires += 1;
    Thread* thread = get_thread(thread_id);
    Resource* resource = get_resource(resource_name);

    pwr_history_sync(thread, resource);

    // Add Resource to Lockset
    if(std::find(thread->lockset.begin(), thread->lockset.end(), resource_name) == thread->lockset.end()) {
        thread->lockset.push_back(resource_name);
    }

    // Set acquire History
    resource->last_acquire = Epoch { thread_id, thread->vector_clock.find(thread_id) };

    thread->write_since_acquire.push_back(false);
    
    thread->vector_clock.increment(thread->id);

    debug_print("acquire", thread_id, resource_name);
}

int cntReleases = 0;
int cntReleasesOnlyReads = 0;

void PWRDetectorOptimized4::release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
    releases += 1;
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
    if(thread->write_since_acquire.back() == true) {
        std::shared_ptr<EpochVCPair> shared_epoch_vc_pair = std::shared_ptr<EpochVCPair>(new EpochVCPair{
            resource->last_acquire,
            thread->vector_clock
        });
        for(auto thread_iter = threads.begin(); thread_iter != threads.end(); ++thread_iter) {
            if(thread_iter->first == thread->id) {
                continue;
            }

            auto current_history = &thread_iter->second.history.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
            if(current_history->size() >= THREAD_HISTORY_SIZE) {
                current_history->pop_back();
            }
            current_history->push_front(shared_epoch_vc_pair);
        }

        auto current_global_history = &global_history.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
        if(current_global_history->size() >= THREAD_HISTORY_SIZE) {
            current_global_history->pop_back();
        }
        current_global_history->push_front(shared_epoch_vc_pair);
    } else {
        cntReleasesOnlyReads += 1;
    }
    cntReleases += 1;

    thread->write_since_acquire.pop_back();

    thread->vector_clock.increment(thread->id);

    debug_print("release", thread_id, resource_name);
}

void PWRDetectorOptimized4::fork_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
    forks += 1;
    Thread* thread = get_thread(thread_id);
    Thread* target_thread = get_thread(target_thread_id);

    target_thread->vector_clock = thread->vector_clock;
    target_thread->vector_clock.increment(target_thread_id);

    thread->vector_clock.increment(thread->id);
}

void PWRDetectorOptimized4::join_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
    joins += 1;
    Thread* thread = get_thread(thread_id);
    Thread* target_thread = get_thread(target_thread_id);

    thread->vector_clock.merge_into(&target_thread->vector_clock);

    thread->vector_clock.increment(thread->id);
}

void PWRDetectorOptimized4::notify_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
    notifiesCnt += 1;
    Thread* thread = get_thread(thread_id);
    
    auto notifies_iter = notifies.find(resource_name);
    if(notifies_iter == notifies.end()) {
        notifies_iter = notifies.insert({ resource_name, VectorClock() }).first;
    }

    notifies_iter->second.merge_into(&thread->vector_clock);
    thread->vector_clock.merge_into(&notifies_iter->second);

    thread->vector_clock.increment(thread_id);
}

void PWRDetectorOptimized4::wait_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
    notifywaits += 1;
    Thread* thread = get_thread(thread_id);
    
    auto notifies_iter = notifies.find(resource_name);
    
    if(notifies_iter == notifies.end()) {
        return;
    }

    thread->vector_clock.merge_into(&notifies_iter->second);
    thread->vector_clock.increment(thread_id);

    notifies[resource_name] = thread->vector_clock;
}

void PWRDetectorOptimized4::get_races() {
    // Statistic reporting.
#ifdef COLLECT_STATISTICS
    this->lockframe->report_statistic("reads", reads);
    this->lockframe->report_statistic("writes", writes);
    this->lockframe->report_statistic("acquires", acquires);
    this->lockframe->report_statistic("releases", releases);
    this->lockframe->report_statistic("forks", forks);
    this->lockframe->report_statistic("joins", joins);
    this->lockframe->report_statistic("notify", notifiesCnt);
    this->lockframe->report_statistic("notifywait", notifywaits);
    this->lockframe->report_statistic("threads", threads.size());
    this->lockframe->report_statistic("resources", resources.size());
    this->lockframe->report_statistic("condCnt", std::vector<size_t> {static_cast<unsigned long>(cond2Cnt), static_cast<unsigned long>(cond1Cnt)});
    this->lockframe->report_statistic("cntReadsNoSync", std::vector<size_t> {static_cast<unsigned long>(cntReadsNoSync), static_cast<unsigned long>(cntReads)});
    this->lockframe->report_statistic("cntReleasesOnlyReads", std::vector<size_t> {static_cast<unsigned long>(cntReleasesOnlyReads), static_cast<unsigned long>(cntReleases)});
#endif //COLLECT_STATISTICS


    // History stats calculation
    int history_res_min = INT32_MAX;
    int history_res_max = 0;
    int history_res_sum = 0;
    int history_res_count = 0;

    int history_min = INT32_MAX;
    int history_max = 0;
    int history_sum = 0;
    int history_count = 0;

    for(auto &[threadid, thread]: threads) {
        history_res_sum += thread.history.size();
        history_res_count += 1;
        if(thread.history.size() < history_res_min) {
            history_res_min = thread.history.size();
        }
        if(thread.history.size() > history_res_max) {
            history_res_max = thread.history.size();
        }

        for(auto &[res, res_history] : thread.history) {
            history_sum += res_history.size();
            history_count += 1;
            if(res_history.size() < history_min) {
                history_min = res_history.size();
            }
            if(res_history.size() > history_max) {
                history_max = res_history.size();
            }
        }
    }

    printf("\"history_stats\": {\n");
    printf("\t\"res_avg\": %.3f,\n", (float)history_res_sum / (float)history_res_count);
    printf("\t\"%d\": {\n", THREAD_HISTORY_SIZE);
    printf("\t\t\"min\": %d,\n", history_min);
    printf("\t\t\"max\": %d,\n", history_max);
    printf("\t\t\"avg\": %.3f\n", (float)history_sum / (float)history_count);
    printf("\t}\n");
    printf("},\n");
    printf("\"benchmark\": {\n");
    printf("},\n");

    /*printf("cond1Cnt / cond2Cnt: %d / %d\n", cond1Cnt, cond2Cnt);
    printf("cntReads / cntReadsNoSync: %d / %d\n", cntReads, cntReadsNoSync);
    printf("cntReleases / cntReleasesOnlyReads: %d / %d\n", cntReleases, cntReleasesOnlyReads);
    printf("threads: %d\n", threads.size());
    printf("resources: %d\n", resources.size());

    printf("\nreads: %d", reads);
    printf("\nwrites: %d", writes);
    printf("\nacquires: %d", acquires);
    printf("\nreleases: %d", releases);
    printf("\nforks: %d", forks);
    printf("\njoins: %d", joins);
    printf("\nnotifiesCnt: %d", notifiesCnt);
    printf("\nnotifywaits: %d\n", notifywaits);

    printf("readReport: %d\n", read_report);
    printf("writeReport: %d\n", write_report);


    int history_res_min = INT32_MAX;
    int history_res_max = 0;
    int history_res_sum = 0;
    int history_res_count = 0;

    int history_min = INT32_MAX;
    int history_max = 0;
    int history_sum = 0;
    int history_count = 0;

    for(auto &[threadid, thread]: threads) {
        history_res_sum += thread.history.size();
        history_res_count += 1;
        if(thread.history.size() < history_res_min) {
            history_res_min = thread.history.size();
        }
        if(thread.history.size() > history_res_max) {
            history_res_max = thread.history.size();
        }

        for(auto &[res, res_history] : thread.history) {
            history_sum += res_history.size();
            history_count += 1;
            if(res_history.size() < history_min) {
                history_min = res_history.size();
            }
            if(res_history.size() > history_max) {
                history_max = res_history.size();
            }
        }
    }

    printf("Resources in History min: %d\n", history_res_min);
    printf("Resources in History max: %d\n", history_res_max);
    printf("Resources in History avg: %f\n", (float)history_res_sum / (float)history_res_count);

    printf("History min: %d\n", history_min);
    printf("History max: %d\n", history_max);
    printf("History avg: %f\n", (float)history_sum / (float)history_count);*/
}