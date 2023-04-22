#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <array>
#include <unordered_map>
#include <filesystem>
#include <unistd.h>
#include <iomanip>
#include "../lockframe.hpp"
#include "../pwrdetector.hpp"
#include "../undead.hpp"
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
#include "../lib/json.hpp"

std::unordered_map<std::string, Detector *> detectors = {
        {"PWR",                new PWRDetector()},
        {"PWROptimized4",      new PWRDetectorOptimized4()},
        {"UNDEAD",             new UNDEADDetector()},
        {"PWRUNDEAD",          new PWRUNDEADDetector()},
        {"PWRUNDEADGuard",     new PWRUNDEADGuardDetector()},
        {"PWRPaper",           new PWRPaper()},
        {"PWRSharedPointer",   new PWRSharedPointer()},
        {"PWRNoSyncs",         new PWRNoSyncs()},
        {"PWRRemoveAfterSync", new PWRRemoveAfterSync()},
        {"PWRRemoveSyncEqual", new PWRRemoveSyncEqual()},
        {"PWRDontAddReads",    new PWRDontAddReads()}};

bool is_detector_supported(std::string detector) {
    return detectors.find(detector) != detectors.end();
}

LockFrame *create_lockframe_with_detector(std::string detector) {
    LockFrame *lockFrame = new LockFrame();
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
        {"r",    "RD"},
        {"w",    "WR"},
        {"fork", "SIG"},
        {"join", "WT"},
        {"acq",  "LK"},
        {"rel",  "UK"}};

TraceLine convert_result_from_std(std::array<std::string, 3> *current_result) {
    TraceLine result = {};

    auto current_std_thread = std_thread_map.find(current_result->at(0));
    if (current_std_thread == std_thread_map.end()) {
        std_thread_map[current_result->at(0)] = std_thread_counter;
        result.thread_id = std_thread_counter;
        std_thread_counter += 1;
    } else {
        result.thread_id = current_std_thread->second;
    }

    auto event_len = current_result->at(1).find("(");
    auto event_type = std_event_map.find(current_result->at(1).substr(0, event_len));
    if (event_type != std_event_map.end()) {
        result.event_type = event_type->second;
        auto target = current_result->at(1).substr(event_len + 1, current_result->at(1).length() - event_len - 2);

        if (result.event_type == "SIG" || result.event_type == "WT") {
            auto current_std_target_thread = std_thread_map.find(target);
            if (current_std_target_thread == std_thread_map.end()) {
                std_thread_map[target] = std_thread_counter;
                result.target = std_thread_counter;
                std_thread_counter += 1;
            } else {
                result.target = current_std_target_thread->second;
            }
        } else {
            auto current_std_lock_id = std_lock_id_map.find(target);
            if (current_std_lock_id == std_lock_id_map.end()) {
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

std::string stringifyStringVector(const std::vector<std::string> &v) {
    std::stringstream stream;
    for (auto string: v) {
        stream << string << " ";
    }
    return stream.str();
}

int main(int argc, char *argv[]) {

    const std::string usageString = "Usage: ./reader [PWR|UNDEAD|PWRUNDEAD] [--speedygo] /path/to/file\n";

    if ((argc < 2)) {
        std::cout << "Not enough arguments specified." << usageString;
        return 1;
    }

    // Unfortunately necessary as switch statements cannot be done easily over strings/char arrays.
    std::map<std::string, int> validCLIFlags = {
            {"--speedygo",   0},
            {"--std",        1},
            {"-v",           2},
            {"--verbose",    2},
            {"--csv",        3},
            {"-o",           4},
            {"--output",     4},
            {"--no-console", 5},
            {"--timestamp",  6},
    };

    std::vector<std::string> enabledDetectors = {};
    bool speedygo_format = false;
    bool std_format = false;
    bool outputToFile = false;
    bool verboseMode = false;
    bool hideResultsFromStdout = false;
    bool csvOutput = false;
    bool addTimestampToOutput = false;
    std::filesystem::path baseOutputPath("./");

    // attempt to extract detectors and flags from command line arguments. The last element should always be the file to be analzyed.
    for (int i = 1; i < argc - 1; i++) {
        if (strlen(argv[i]) >= 2 && strncmp("-", argv[i], 1) == 0) {
            // leading dashes suggest a flag
            auto foundFlag = validCLIFlags.find(std::string(argv[i]));
            if (foundFlag == validCLIFlags.end()) {
                throw "Invalid flag specified.";
            }

            switch (foundFlag->second) {
                case 0: // --speedygo input of file is expected to be in SpeedyGo format
                    speedygo_format = true;
                    break;
                case 1: // --std input of file is expected to be in STD format
                    std_format = true;
                    break;
                case 2: // -v / --verbose adds additional messages to the console like progress reports.
                    verboseMode = true;
                    break;
                case 3: // --csv changes the output format to a comma-seperated variant.
                    csvOutput = true;
                    break;
                case 4: // -o / --output Enables writing the results to files in the specified directory.
                    if (outputToFile)
                        break; // Ignore subsequent output flags!
                    outputToFile = true;
                    baseOutputPath = std::filesystem::path(argv[i + 1]);
                    i++; // since we assume the follow up value to be the actual output, we skip an iteration
                    break;
                case 5: // disables the results getting printed to the console.
                    hideResultsFromStdout = true;
                    break;
                case 6:
                    addTimestampToOutput = true;
                    break;

            }
        } else if (is_detector_supported(std::string(argv[i]))) {
            enabledDetectors.push_back(std::string(argv[i]));
        } else {
            std::cout << "An invalid argument " << argv[i] << " was specified." << std::endl;
            exit(1);
        }
    }

    // If neither the console will show any results nor any output file was enabled, exit.
    if (hideResultsFromStdout && !outputToFile) {
        std::cout << "All possible outputs were disabled. Hint: specify an output file with -o or remove --no-console. "
                  << usageString;
        return 1;
    }

    // If an output emitting a file is enabled, check if the output directory is writable.
    if (outputToFile) {
        if (!std::filesystem::is_directory(baseOutputPath)) {
            std::cout << "The given output path " << baseOutputPath << " is not a directory." << std::endl;
            return 1;
        }

        // yes, you need to do two conversions. and yes, this is cursed.
        if (access(baseOutputPath.string().c_str(), W_OK) != 0) {
            std::cout << "The given output path " << baseOutputPath << " cannot be written to." << std::endl;
            return 1;
        };
    }

    // If no detector was found, exit the program.
    if (enabledDetectors.empty()) {
        std::cout << "No valid detectors were specified. " << usageString;
        return 1;
    }

    std::filesystem::path tracePath(argv[argc - 1]);

    std::cout << "Analyzing trace file " << tracePath.filename().string() << std::endl
              << "Enabled detectors: " << stringifyStringVector(enabledDetectors) << std::endl
              << "Verbose: " << verboseMode << " CSV: " << csvOutput << std::endl;

    // Main program loop. Iterate over each supplied detector.
    for (auto &detectorName: enabledDetectors) {
        // attempt to get a file stream from the provided *last* argument (argc - 1). If the file is not good (any error flags), exit program
        std::ifstream file(tracePath);
        if (!file.good()) {
            std::cout << "The specified trace file " << tracePath << " cannot be found." << std::endl;
            return 1;
        }
        std::cout << "Beginning analysis using " << detectorName << std::endl;
        // create a new lockframe instance with the passed detector argument, and store a start_tim
        LockFrame *lockFrame = create_lockframe_with_detector(detectorName);
        auto start_time = std::chrono::steady_clock::now();

        // instantiate line as where the program will store the currently processed line
        std::string line;
        int line_index = 0;
        // If the file is in std_format, the separator is a pipe. Otherwise assume commas.
        const char separator = std_format ? '|' : ',';

        // read out the passed file line for line
        while (std::getline(file, line)) {
            line_index++;
            std::stringstream ss(line.c_str());
            std::array<std::string, 3> result{"", "", ""};

            // As long as the string is good, process the first three entries on the line. (There should always be three entries)
            // This sets the previously instantiated result at position i to the result at that position.
            int good_count = 0;
            while (ss.good() && good_count < 3) {
                std::string substring;
                getline(ss, substring, separator);
                result[good_count] = substring;
                good_count += 1;
            }

            // If the last element in the results array is still blank, then the file must've been malformed. Exit.
            if (result[2] == "") {
                std::cout << "Bad file format on line " << line_index << ": " << line << std::endl;
                return 1;
            }

            // Attempt to convert the split line into the internal representation.. Any thrown errors will exit the program.
            try {
                TraceLine trace_line;
                if (std_format) {
                    // If we have the std-format set, we convert it in-place.
                    trace_line = convert_result_from_std(&result);
                } else {
                    // Otherwise, we construct a simple tuple that converts the string numbers to integers.
                    trace_line = {std::stoi(result[0]), result[1], std::stoi(result[2])};
                }

                // Pass the found events to the lockframe detector through function calls.
                if (trace_line.event_type == "LK") {
                    lockFrame->acquire_event(trace_line.thread_id, line_index, trace_line.target);
                } else if (trace_line.event_type == "UK") {
                    lockFrame->release_event(trace_line.thread_id, line_index, trace_line.target);
                } else if (trace_line.event_type == "RD") {
                    lockFrame->read_event(trace_line.thread_id, line_index, trace_line.target);
                } else if (trace_line.event_type == "WR") {
                    lockFrame->write_event(trace_line.thread_id, line_index, trace_line.target);
                } else if (trace_line.event_type == "SIG") {
                    if (speedygo_format) {
                        signal_list[trace_line.target] = trace_line.thread_id;
                    } else {
                        lockFrame->fork_event(trace_line.thread_id, line_index, trace_line.target);
                    }
                } else if (trace_line.event_type == "WT") {
                    if (speedygo_format) {
                        auto thread_to_fork_from = signal_list.find(trace_line.target);
                        if (thread_to_fork_from != signal_list.end()) {
                            lockFrame->fork_event(thread_to_fork_from->second, line_index, trace_line.thread_id);
                        }
                    } else {
                        lockFrame->join_event(trace_line.thread_id, line_index, trace_line.target);
                    }
                } else if (trace_line.event_type == "NT") {
                    lockFrame->notify_event(trace_line.thread_id, line_index, trace_line.target);
                } else if (trace_line.event_type == "NTWT") {
                    lockFrame->wait_event(trace_line.thread_id, line_index, trace_line.target);
                } else if (trace_line.event_type == "AWR" || trace_line.event_type == "ARD") {
                    // TODO: implement Atomic events
                    // std::cout << "Atomic not implemented " << line_index <<  ": " << line << std::endl;
                } else { // no valid event type found, assume bad file format.
                    throw "bad file format";
                }
            }
            catch (...) { // failing to parse should crash the whole thing.
                std::cout << "Bad file format on line " << line_index << ": " << line << std::endl;
                return 1;
            }

            // occasional reporting on progress - report to stdout every million lines
            if (verboseMode && line_index % 1000000 == 0) {
                std::cout << "Parsed line " << line_index << std::endl;
            }
        }

        // set the parse time finish.
        auto end_time = std::chrono::steady_clock::now();
        std::cout << "File parsing for the detector " << detectorName << " has finished. Analysis commences now."
                  << std::endl;

        // Perform the actual race calculation on the lockFrame implementation / detector and store them in races
        std::vector<DataRace> races = lockFrame->get_races();
        std::cout << detectorName << " has concluded analysis." << std::endl;

        // hint message about output not getting dumped into console.
        if (hideResultsFromStdout) {
            std::cout << "Results will only be written to the specified output directory." << std::endl;
        }

        std::ofstream raceOutput;
        if (outputToFile) {
            std::stringstream fileName;
            fileName << "/" << detectorName << "_" << tracePath.filename().string();
            if (addTimestampToOutput) {
                fileName << "_";
                auto t = std::time(nullptr);
                auto tm = *std::localtime(&t);
                fileName << std::put_time(&tm, "%d-%m-%Y_%H-%M-%S");
            }
            if (csvOutput)
                fileName << ".csv";
            else
                fileName << ".txt";

            std::filesystem::path racePath(baseOutputPath.string() + fileName.str());
            raceOutput.open(racePath);
        }
        // Report all races as specified by the user
        for (auto &race: races) {
            std::stringstream raceStream;
            if (csvOutput) {
                // CSV-compatible output format. Reports as THREAD1, THREAD2, RESOURCENAME, TRACELINE
                raceStream << race.thread_id_1 << "," << race.thread_id_2 << "," << race.resource_name << ","
                           << race.trace_position << std::endl;
            } else {
                // original format established by Jan Metzger.
                raceStream << "T" << race.thread_id_1 << " <--> T" << race.thread_id_2 << ", Resource: ["
                           << race.resource_name << "], Line: " << race.trace_position << std::endl;
            }
            // Write results to outputs
            if (!hideResultsFromStdout)
                std::cout << raceStream.str();
            if (outputToFile)
                raceOutput << raceStream.str();
        }
        if (outputToFile)
            raceOutput.close(); // close output file if it was necessary.

#ifdef COLLECT_STATISTICS
        // Report statistics, if defined during compile time.
        std::ofstream statOutput;
        if (outputToFile) {
            std::stringstream fileName;
            fileName << "/" << detectorName << "_STATS_" << tracePath.filename().string();
            if (addTimestampToOutput) {
                fileName << "_";
                auto t = std::time(nullptr);
                auto tm = *std::localtime(&t);
                fileName << std::put_time(&tm, "%d-%m-%Y_%H-%M-%S");
            }
            if (csvOutput)
                fileName << ".csv";
            else
                fileName << ".txt";

            std::filesystem::path statPath(baseOutputPath.string() + fileName.str());
            statOutput.open(statPath);
        }
        for (auto &stat: lockFrame->statistics) {
            std::stringstream statStream;
            if (csvOutput) {
                statStream << stat.statistics_key << "," << stat.statistics_value << std::endl;
            } else {
                statStream << stat.statistics_key << ": " << stat.statistics_value << std::endl;
            }

            if (!hideResultsFromStdout) {
                std::cout << statStream.str();
            }

            if (outputToFile) {
                statOutput << statStream.str();
            }

        }
        if (outputToFile)
            statOutput.close();

#endif // COLLECT_STATISTICS

        std::cout << "Parsed " << line_index << " lines in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() << "ms."
                  << std::endl;
        std::cout << "Found " << races.size() << " races." << std::endl;
    }

    return 0;
}