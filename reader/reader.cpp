#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <array>
#include <unordered_map>
#include "../lockframe.hpp"
#include "../pwrdetector.hpp"
#include "../undead.hpp"
#include "../undeadlt.hpp"
#include "../pwrundeaddetector.cpp"
#include "../pwrundeadguarddetector.cpp"
#include "../detector.hpp"
#include "../debug/pwrdetector_optimized_4.hpp"
#include "../debug/pwr_paper.cpp"
#include "../debug/pwr_shared_ptr.hpp"
#include "../debug/pwr_no_syncs.cpp"
#include "../debug/pwr_remove_after_sync.cpp"
#include "../debug/pwr_remove_sync_equal.cpp"
#include "../debug/pwr_dont_add_reads.cpp"

std::unordered_map<std::string, Detector*> detectors = {
    {"PWR", new PWRDetector()},
    {"PWROptimized4", new PWRDetectorOptimized4()},
    {"UNDEAD", new UNDEADDetector()},
    {"PWRUNDEAD", new PWRUNDEADDetector()},
    {"PWRUNDEADGuard", new PWRUNDEADGuardDetector()},
    {"UNDEADLT", new UNDEADLTDetector()},
    {"PWRPaper", new PWRPaper()},
    {"PWRSharedPointer", new PWRSharedPointer()},
    {"PWRNoSyncs", new PWRNoSyncs()},
    {"PWRRemoveAfterSync", new PWRRemoveAfterSync()},
    {"PWRRemoveSyncEqual", new PWRRemoveSyncEqual()},
    {"PWRDontAddReads", new PWRDontAddReads()}
};

bool is_detector_supported(std::string detector) {
    return detectors.find(detector) != detectors.end();
}

LockFrame* create_lockframe_with_detector(std::string detector) {
    LockFrame* lockFrame = new LockFrame();
    lockFrame->set_detector(detectors.find(detector)->second);
    return lockFrame;
}

struct TraceLine {
    int thread_id;
    std::string event_type;
    int target;
};

std::unordered_map<int, int> signal_list = {};

int std_lock_id_counter = 1;
std::unordered_map<std::string, int> std_lock_id_map = {};
int std_thread_counter = 1;
std::unordered_map<std::string, int> std_thread_map = {};
std::unordered_map<std::string, std::string> std_event_map = {
    {"r", "RD"}, {"w", "WR"}, {"fork", "SIG"}, {"join", "WT"}, {"acq", "LK"}, {"rel", "UK"}
};

TraceLine convert_result_from_std(std::array<std::string, 3>* current_result) {
    TraceLine result = {};
    
    auto current_std_thread = std_thread_map.find(current_result->at(0));
    if(current_std_thread == std_thread_map.end()) {
        std_thread_map[current_result->at(0)] = std_thread_counter;
        result.thread_id = std_thread_counter;
        std_thread_counter += 1;
    } else {
        result.thread_id = current_std_thread->second;
    }

    auto event_len = current_result->at(1).find("(");
    auto event_type = std_event_map.find(current_result->at(1).substr(0, event_len));
    if(event_type != std_event_map.end()) {
        result.event_type = event_type->second;
        auto target = current_result->at(1).substr(event_len + 1, current_result->at(1).length() - event_len - 2);

        if(result.event_type == "SIG" || result.event_type == "WT") {
            auto current_std_target_thread = std_thread_map.find(target);
            if(current_std_target_thread == std_thread_map.end()) {
                std_thread_map[target] = std_thread_counter;
                result.target = std_thread_counter;
                std_thread_counter += 1;
            } else {
                result.target = current_std_target_thread->second;
            }
        } else {
            auto current_std_lock_id = std_lock_id_map.find(target);
            if(current_std_lock_id == std_lock_id_map.end()) {
                std_lock_id_map[target] = std_lock_id_counter;
                result.target = std_lock_id_counter;
                std_lock_id_counter += 1;
            } else {
                result.target = current_std_lock_id->second;
            }
        }
    }

    return result;
}

int main(int argc, char *argv[]) {
    if((argc != 3 && (argc != 4 && std::string(argv[2]) != std::string("--speedygo"))) || !is_detector_supported(std::string(argv[1]))) {
        std::cout << "./reader [PWR|UNDEAD|PWRUNDEAD] [--speedygo] /path/to/file" << std::endl;
        return 1;
    }

    bool speedygo_format = argc == 4 && std::string(argv[2]) == std::string("--speedygo");
    bool std_format = argc == 4 && std::string(argv[2]) == std::string("--std");

    std::ifstream file(argv[argc - 1]);
    if(!file.good()) {
        std::cout << "Can't find file" << std::endl;
        return 1;
    }

    LockFrame* lockFrame = create_lockframe_with_detector(argv[1]);
    auto start_time = std::chrono::steady_clock::now();
    
    std::string line;
    int line_index = 0;
    const char separator = std_format ? '|' : ',';
    while(std::getline(file, line)) {
        line_index++;
        std::stringstream ss(line.c_str());
        std::array<std::string, 3> result{"", "", ""};

        int good_count = 0;
        while(ss.good() && good_count < 3) {
            std::string substring;
            getline(ss, substring, separator);
            result[good_count] = substring;
            good_count += 1;
        }

        if(result.size() < 3) {
            std::cout << "Bad file format on line " << line_index << ": " << line << std::endl;
            return 1;
        }

        try {
            TraceLine trace_line;
            if(std_format) {
                trace_line = convert_result_from_std(&result);
            } else {
                trace_line = { std::stoi(result[0]), result[1], std::stoi(result[2]) };
            }

            if(trace_line.event_type == "LK") {
                lockFrame->acquire_event(trace_line.thread_id, line_index, trace_line.target);
            } else if(trace_line.event_type == "UK") {
                lockFrame->release_event(trace_line.thread_id, line_index, trace_line.target);
            } else if(trace_line.event_type == "RD") {
                lockFrame->read_event(trace_line.thread_id, line_index, trace_line.target);
            } else if(trace_line.event_type == "WR") {
                lockFrame->write_event(trace_line.thread_id, line_index, trace_line.target);
            } else if(trace_line.event_type == "SIG") {
                if(speedygo_format) {
                    signal_list[trace_line.target] = trace_line.thread_id;
                } else {
                    lockFrame->fork_event(trace_line.thread_id, line_index, trace_line.target);
                }
            } else if(trace_line.event_type == "WT") {
                if(speedygo_format) {
                    auto thread_to_fork_from = signal_list.find(trace_line.target);
                    if(thread_to_fork_from != signal_list.end()) {
                        lockFrame->fork_event(thread_to_fork_from->second, line_index, trace_line.thread_id);
                    }
                } else {
                    lockFrame->join_event(trace_line.thread_id, line_index, trace_line.target);
                }
            } else if(trace_line.event_type == "NT") {
                lockFrame->notify_event(trace_line.thread_id, line_index, trace_line.target);
            } else if(trace_line.event_type == "NTWT") {
                lockFrame->wait_event(trace_line.thread_id, line_index, trace_line.target);
            } else if(trace_line.event_type == "AWR" || trace_line.event_type == "ARD") {
                //std::cout << "Atomic not implemented " << line_index <<  ": " << line << std::endl;
            } else {
                throw "bad file format";
            }
        } catch(...) {
            std::cout << "Bad file format on line " << line_index << ": " << line << std::endl;
            return 1;
        }

        if(line_index % 1000000 == 0) {
            std::cout << "Parsed line " << line_index << std::endl;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    
    std::vector<DataRace> races = lockFrame->get_races();

    for(auto &race : races) {
        std::cout << "T" << race.thread_id_1 << " <--> T" << race.thread_id_2 << ", Resource: [" << race.resource_name << "], Line: " << race.trace_position << std::endl;
    }

    std::cout << "Parsed " << line_index << " lines in " << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() << "ms." << std::endl;
    std::cout << "Found " << races.size() << " races." << std::endl;

    return 0;
}