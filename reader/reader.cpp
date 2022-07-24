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
#include "../pwrundeaddetector.cpp"
#include "../detector.hpp"
#include "../debug/pwrdetector_optimized_4.hpp"
#include "../debug/pwr_paper.cpp"
#include "../debug/pwr_shared_ptr.cpp"
#include "../debug/pwr_no_syncs.cpp"
#include "../debug/pwr_remove_after_sync.cpp"
#include "../debug/pwr_remove_sync_equal.cpp"
#include "../debug/pwr_dont_add_reads.cpp"

std::unordered_map<std::string, Detector*> detectors = {
    {"PWR", new PWRDetector()},
    {"PWROptimized4", new PWRDetectorOptimized4()},
    {"UNDEAD", new UNDEADDetector()},
    {"PWRUNDEAD", new PWRUNDEADDetector()},
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

std::unordered_map<int, int> signal_list = {};


int main(int argc, char *argv[]) {
    if((argc != 3 && (argc != 4 && std::string(argv[2]) != std::string("--speedygo"))) || !is_detector_supported(std::string(argv[1]))) {
        std::cout << "./reader [PWR|UNDEAD|PWRUNDEAD] [--speedygo] /path/to/file" << std::endl;
        return 1;
    }

    bool speedygo_format = argc == 4 && std::string(argv[2]) == std::string("--speedygo");

    std::ifstream file(argv[argc - 1]);
    if(!file.good()) {
        std::cout << "Can't find file" << std::endl;
        return 1;
    }

    LockFrame* lockFrame = create_lockframe_with_detector(argv[1]);
    auto start_time = std::chrono::steady_clock::now();
    
    std::string line;
    int line_index = 0;
    while(std::getline(file, line)) {
        line_index++;
        std::stringstream ss(line.c_str());
        std::array<std::string, 3> result{"", "", ""};

        int good_count = 0;
        while(ss.good() && good_count < 3) {
            std::string substring;
            getline(ss, substring, ',');
            result[good_count] = substring;
            good_count += 1;
        }

        if(result.size() < 3) {
            std::cout << "Bad file format on line " << line_index << ": " << line << std::endl;
            return 1;
        }

        try {
            int thread_id = std::stoi(result[0]);

            if(result[1] == "LK") {
                lockFrame->acquire_event(thread_id, line_index, std::stoi(result[2]));
            } else if(result[1] == "UK") {
                lockFrame->release_event(thread_id, line_index, std::stoi(result[2]));
            } else if(result[1] == "RD") {
                lockFrame->read_event(thread_id, line_index, std::stoi(result[2]));
            } else if(result[1] == "WR") {
                lockFrame->write_event(thread_id, line_index, std::stoi(result[2]));
            } else if(result[1] == "SIG") {
                if(speedygo_format) {
                    signal_list[std::stoi(result[2])] = std::stoi(result[0]);
                } else {
                    lockFrame->fork_event(thread_id, line_index, std::stoi(result[2]));
                }
            } else if(result[1] == "WT") {
                if(speedygo_format) {
                    auto thread_to_fork_from = signal_list.find(std::stoi(result[2]));
                    if(thread_to_fork_from != signal_list.end()) {
                        lockFrame->fork_event(thread_to_fork_from->second, line_index, thread_id);
                    }
                } else {
                    lockFrame->join_event(thread_id, line_index, std::stoi(result[2]));
                }
            } else if(result[1] == "NT") {
                lockFrame->notify_event(thread_id, line_index, std::stoi(result[2]));
            } else if(result[1] == "NTWT") {
                lockFrame->wait_event(thread_id, line_index, std::stoi(result[2]));
            } else if(result[1] == "AWR" || result[1] == "ARD") {
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