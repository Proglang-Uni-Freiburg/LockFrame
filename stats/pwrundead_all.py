import subprocess
import pathlib
import os
import time
import pandas
import re
import matplotlib.pyplot as plt
from cycler import cycler
import numpy as np

RESULTS_PATH = pathlib.Path(__file__).parent.joinpath('results').joinpath(pathlib.Path(__file__).stem).resolve()
READER_PATH = pathlib.Path(__file__).parent.parent.joinpath('reader').resolve()
LOG_FILES_DIR = '/home/jan/Dev/traces/decapo'
LOG_FILES = [
    'account.std', 'airlinetickets.std', 'array.std', 'boundedbuffer.std',
    'bubblesort.std', 'bufwriter.std', 'clean.std', 'critical.std', 
    'derby.std', 'ftpserver.std', 'jigsaw.std', 'lang.std',
    'linkedlist.std', 'lufact.std', 'luindex.std', 'lusearch.std',
    'mergesort.std', 'moldyn.std', 'pingpong.std', 'producerconsumer.std',
    'raytracer.std', 'readerswriters.std', 'sor.std', 'sunflow.std',
    'tsp.std', 'twostage.std', 'wronglock.std'
]

#LOG_FILES_DIR = '/home/jan/Dev/traces/speedygo'
#LOG_FILES = ['avroraNT.log', 'batikNT.log', 'cryptNT.log', 'h2NT.log', 'jythonNT.log', 'lufactNT.log', 'lusearchNT.log', 'pmdNT.log', 'sunflowNTABORTED.log', 'tomcatFull.log', 'xalanFull.log']
LOG_FILES_SPEEDYGO_FORMAT = 0
LOG_FILES_STD_FORMAT = 1

def check_return_code(result):
    if result.returncode != 0:
        raise Exception(f'Error while running "{ " ".join(result.args) } "' )

def build(limit):
    print("Building...")
    result = subprocess.run(['cmake', '-DCMAKE_BUILD_TYPE=Release', '-DCOLLECT_STATISTICS=1', '-DPWRUNDEADDETECTOR_VC_PER_DEP_LIMIT=' + str(limit), '.'], stdout=subprocess.PIPE, cwd=READER_PATH)
    check_return_code(result)
    result = subprocess.run(['cmake', '--build', '.'], stdout=subprocess.PIPE, cwd=READER_PATH)
    check_return_code(result)
    print("Build done\n---\n")

def run_by_detector(detector, fullpath):
    print("Running detector: " + detector)
    args = ['./reader', detector]
    if LOG_FILES_SPEEDYGO_FORMAT:
        args.append('--speedygo')
    elif LOG_FILES_STD_FORMAT:
        args.append('--std')
    args.append(fullpath)
    return subprocess.run(args, stdout=subprocess.PIPE, cwd=READER_PATH, text=True)

def build_and_write_results(limit):
    build(limit)
    with open(os.path.join(RESULTS_PATH, 'results.csv'), 'a', buffering=1) as results_file:
        results_file.write("trace,vc_limit,races_undead,races_pwrundead,races_pwrundeadextra,time_taken_full_undead,time_taken_full_pwrundead,time_taken_full_pwrundeadextra,time_taken_phase2_undead,time_taken_phase2_pwrundead,time_taken_phase2_pwrundeadextra,undead_deps_thread,pwr_deps_thread,extra_deps_thread,vc_limit_exceeded,vc_limit_exceeded_dep_counter,locks,reads,writes,acquires,releases,forks,joins,notifies,waits\n")
        for logfile in LOG_FILES:
            print("Running benchmark of " + logfile)
            fullpath = os.path.join(LOG_FILES_DIR, logfile)

            results_file.write(logfile + ",")
            results_file.write(str(limit) + ",")

            timeBefore = time.time_ns()
            result_undead = run_by_detector("UNDEAD", fullpath)
            timeTaken_undead = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result_undead)

            timeBefore = time.time_ns()
            result_pwrundead = run_by_detector("PWRUNDEAD", fullpath)
            timeTaken_pwrundead = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result_pwrundead)

            timeBefore = time.time_ns()
            result_pwrundeadextra = run_by_detector("PWRUNDEAD3", fullpath)
            timeTaken_pwrundeadextra = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result_pwrundeadextra)

            results_file.write(re.search(r"^Found (\d+) races.$", result_undead.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^Found (\d+) races.$", result_pwrundead.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^Found (\d+) races.$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")

            results_file.write(str(timeTaken_undead) + ',')
            results_file.write(str(timeTaken_pwrundead) + ',')
            results_file.write(str(timeTaken_pwrundeadextra) + ',')

            results_file.write(re.search(r"^Phase 2 elapsed time in milliseconds: (\d+)$", result_undead.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^Phase 2 elapsed time in milliseconds: (\d+)$", result_pwrundead.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^Phase 2 elapsed time in milliseconds: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")

            results_file.write(re.search(r"^UNDEAD dependencies per thread: ([(\d)| ]+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^PWRUNDEAD dependencies per thread: ([(\d)| ]+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^EXTRAEDGES dependencies per thread: ([(\d)| ]+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^VC limit exceeded: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^VC limit exceeded by dependency: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^locks: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^reads: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^writes: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^acquires: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^releases: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^forks: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^joins: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^notifies: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(re.search(r"^waits: (\d+)$", result_pwrundeadextra.stdout, flags=re.MULTILINE).group(1) + "\n")

def create_graph_boxplot(results, key, min = 50):
    logfile_stats = {}
    for logfile in results:
        stats = results[logfile][key]
        stats_sum = sum(stats)
        if stats_sum < min:
            continue
        stats = [x / stats_sum * 100 for x in stats]
        if len(stats) > 2:
            logfile_stats[logfile] = stats

    plt.boxplot(list(logfile_stats.values()), labels=list(logfile_stats.keys()), vert=False)
    plt.savefig(os.path.join(RESULTS_PATH, key + '_boxplot.png'), bbox_inches='tight')
    plt.close()

def create_graph_time_taken_phase_2_relation(results):
    logfiles = {}

    for result in results:
        if results[result]['time_taken_full_undead'] < 20:
            continue

        logfiles[result] = {
            'relative_time_taken_phase2_undead': results[result]['time_taken_phase2_undead'] / results[result]['time_taken_full_undead'] * 100,
            'relative_time_taken_phase2_pwrundead': results[result]['time_taken_phase2_pwrundead'] / results[result]['time_taken_full_pwrundead'] * 100,
            'relative_time_taken_phase2_pwrundeadextra': results[result]['time_taken_phase2_pwrundeadextra'] / results[result]['time_taken_full_pwrundeadextra'] * 100
        }


    df = pandas.DataFrame({
        'UNDEAD': [logfiles[x]['relative_time_taken_phase2_undead'] for x in logfiles],
        'UNDEAD_PWR': [logfiles[x]['relative_time_taken_phase2_pwrundead'] for x in logfiles],
        'UNDEAD_PWR_EXTRA_EARLY': [logfiles[x]['relative_time_taken_phase2_pwrundeadextra'] for x in logfiles],
    }, index=[x for x in logfiles])

    ax = df.plot.bar()
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=4, fancybox=True)
    ax.set_xlabel("Logfiles")
    ax.set_ylabel("Ausführungszeit in [%]")
    plt.setp(ax.get_xticklabels(), rotation=30, horizontalalignment='right')
    plt.margins(y=0.1)
    plt.savefig(os.path.join(RESULTS_PATH, 'time_taken_phase_2_relation.png'), bbox_inches='tight')
    plt.close()

def create_graph_time_taken(results):
    logfiles = {}
    for result in results:
        if results[result]['time_taken_full_undead'] < 20:
            continue

        fullTime = results[result]['time_taken_full_pwrundeadextra']
        undeadTime = int(results[result]['time_taken_full_undead']) / fullTime * 100
        pwroverheadTime = (results[result]['time_taken_full_pwrundead'] / fullTime * 100) - undeadTime
        extraedgesoverheadTime = 100 - undeadTime - pwroverheadTime

        logfiles[result] = {
            'undead': undeadTime,
            'pwrundead': pwroverheadTime,
            'pwrundeadextra': extraedgesoverheadTime
        }


    df = pandas.DataFrame({
        'UNDEAD': [logfiles[x]['undead'] for x in logfiles],
        'UNDEAD_PWR': [logfiles[x]['pwrundead'] for x in logfiles],
        'UNDEAD_PWR_EXTRA_EARLY': [logfiles[x]['pwrundeadextra'] for x in logfiles],
    }, index=[x for x in logfiles])

    ax = df.plot.bar(stacked=True)
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=4, fancybox=True)
    ax.set_xlabel("Logfiles")
    ax.set_ylabel("Ausführungszeit in [%]")
    plt.setp(ax.get_xticklabels(), rotation=30, horizontalalignment='right')
    plt.margins(y=0.1)
    plt.savefig(os.path.join(RESULTS_PATH, 'time_taken_relation.png'), bbox_inches='tight')
    plt.close()

def create_graph_event_types(results):
    index = results.keys()
    data = {
        'reads': [],
        'writes': [],
        'acquires': [],
        'releases': [],
        'forks': [],
        'joins': [],
        'notifies': [],
        'waits': []
    }

    for trace in results.values():
        event_count = trace['reads'] + trace['writes'] + trace['acquires'] + trace['releases'] + trace['forks'] + trace['joins'] + trace['notifies'] + trace['waits']
        data['reads'].append(trace['reads'] / event_count * 100)
        data['writes'].append(trace['writes'] / event_count * 100)
        data['acquires'].append(trace['acquires'] / event_count * 100)
        data['releases'].append(trace['releases'] / event_count * 100)
        data['forks'].append(trace['forks'] / event_count * 100)
        data['joins'].append(trace['joins'] / event_count * 100)
        data['notifies'].append(trace['notifies'] / event_count * 100)
        data['waits'].append(trace['waits'] / event_count * 100)

    df = pandas.DataFrame(data, index=index)
    ax = df.plot.barh(stacked=True, rot=0)
    ax.legend(loc='upper center', bbox_to_anchor=(
        0.5, 1.2), ncol=4, fancybox=True)
    ax.set_ylabel("[%]", loc="top", rotation="horizontal")
    ax.yaxis.set_label_coords(1, -0.065)
    plt.margins(y=0.1)
    plt.subplots_adjust(top=0.82, left=0.2)
    plt.savefig(os.path.join(RESULTS_PATH, 'event_types.png'), bbox_inches='tight')
    plt.close()

def create_graphs_from_results(results):
    create_graph_event_types(results)
    create_graph_time_taken_phase_2_relation(results)
    create_graph_time_taken(results)
    create_graph_boxplot(results, 'undead_deps_thread', 50)
    create_graph_boxplot(results, 'pwr_deps_thread', 1000)
    create_graph_boxplot(results, 'extra_deps_thread', 50)

def create_deps_table(results):
    with open(os.path.join(RESULTS_PATH, 'deps_table.csv'), 'w') as results_file:
        results_file.write('trace,undead_deps_sum,undead_deps_thread,pwr_deps_sum,pwr_deps_thread,extra_deps_sum,extra_deps_thread\n')
        for trace in results:
            if sum(results[trace]['undead_deps_thread']) == 0:
                continue
            results_file.write(trace + ',')
            results_file.write(str(sum(results[trace]['undead_deps_thread'])) + ',')
            results_file.write('-'.join([str(x) for x in results[trace]['undead_deps_thread']]) + ',')
            results_file.write(str(sum(results[trace]['pwr_deps_thread'])) + ',')
            results_file.write('-'.join([str(x) for x in results[trace]['pwr_deps_thread']]) + ',')
            results_file.write(str(sum(results[trace]['extra_deps_thread'])) + ',')
            results_file.write('-'.join([str(x) for x in results[trace]['extra_deps_thread']]) + '\n')

def create_events_table(results):
    with open(os.path.join(RESULTS_PATH, 'events_table.csv'), 'w') as results_file:
        results_file.write('trace,reads,writes,acquires,releases,forks,joins,notifies,waits\n')
        for key, trace in results.items():
            results_file.write(key + ',')
            results_file.write(str(trace['reads']) + ',')
            results_file.write(str(trace['writes']) + ',')
            results_file.write(str(trace['acquires']) + ',')
            results_file.write(str(trace['releases']) + ',')
            results_file.write(str(trace['forks']) + ',')
            results_file.write(str(trace['joins']) + ',')
            results_file.write(str(trace['notifies']) + ',')
            results_file.write(str(trace['waits']) + '\n')

def create_trace_info_table(results):
    with open(os.path.join(RESULTS_PATH, 'trace_infos.csv'), 'w') as results_file:
        results_file.write('trace,races_undead,races_pwrundead,races_pwrundeadextra,events,locks\n')
        for key, trace in results.items():
            event_count = trace['reads'] + trace['writes'] + trace['acquires'] + trace['releases'] + trace['forks'] + trace['joins'] + trace['notifies'] + trace['waits']
            results_file.write(key + ',')
            results_file.write(str(trace['races_undead']) + ',')
            results_file.write(str(trace['races_pwrundead']) + ',')
            results_file.write(str(trace['races_pwrundeadextra']) + ',')
            results_file.write(str(event_count) + ',')
            results_file.write(str(trace['locks']) + '\n')

def create_timing_table(results):
    with open(os.path.join(RESULTS_PATH, 'timing_table.csv'), 'w') as results_file:
        results_file.write('trace,detector,time_taken_full (ms),time_taken_phase_1 (ms),time_taken_phase_2 (ms)\n')
        for key, trace in results.items():
            if trace['time_taken_full_undead'] < 10:
                continue
            for detectorKey, detectorName in [('undead', 'UNDEAD'), ('pwrundead', 'UNDEAD_PWR'), ('pwrundeadextra', 'UNDEAD_PWR_EXTRA_EARLY')]:
                results_file.write(key + ',')
                results_file.write(detectorName + ',')
                results_file.write(str(trace['time_taken_full_' + detectorKey]) + ',')
                results_file.write(str(trace['time_taken_full_' + detectorKey] - trace['time_taken_phase2_' + detectorKey]) + ',')
                results_file.write(str(trace['time_taken_phase2_' + detectorKey]) + '\n')

def create_vc_exceeded_table(results):
    with open(os.path.join(RESULTS_PATH, 'vc_exceeded_table.csv'), 'w') as results_file:
        results_file.write('trace,vc_limit_exceeded_counter,vc_limit_exceeded_deps_counter,percentage of deps, percentage of deps extra\n')
        for key, trace in results.items():
            if trace['vc_limit_exceeded'] < 10:
                continue
            results_file.write(key + ',')
            results_file.write(str(trace['vc_limit_exceeded']) + ',')
            results_file.write(str(trace['vc_limit_exceeded_dep_counter']) + ',')
            results_file.write("{:.2f}".format(trace['vc_limit_exceeded_dep_counter'] / sum(trace['pwr_deps_thread']) * 100) + ',')
            results_file.write("{:.2f}".format(trace['vc_limit_exceeded_dep_counter'] / (sum(trace['pwr_deps_thread']) + sum(trace['extra_deps_thread'])) * 100) + '\n')

def read_results():
    logfiles = {}
    first = True
    for line in open(os.path.join(RESULTS_PATH, 'results.csv'), 'r').readlines():
        if first:
            first = False
            continue
        data = line.split(',')
        logfiles[data[0]] = {
            'vc_limit': data[1],
            'races_undead': int(data[2]),
            'races_pwrundead': int(data[3]),
            'races_pwrundeadextra': int(data[4]),
            'time_taken_full_undead': int(data[5]),
            'time_taken_full_pwrundead': int(data[6]),
            'time_taken_full_pwrundeadextra': int(data[7]),
            'time_taken_phase2_undead': int(data[8]),
            'time_taken_phase2_pwrundead': int(data[9]),
            'time_taken_phase2_pwrundeadextra': int(data[10]),
            'undead_deps_thread': [int(x) for x in data[11].split(' ')],
            'pwr_deps_thread': [int(x) for x in data[12].split(' ')],
            'extra_deps_thread': [int(x) for x in data[13].split(' ')],
            'vc_limit_exceeded': int(data[14]),
            'vc_limit_exceeded_dep_counter': int(data[15]),
            'locks': int(data[16]),
            'reads': int(data[17]),
            'writes': int(data[18]),
            'acquires': int(data[19]),
            'releases': int(data[20]),
            'forks': int(data[21]),
            'joins': int(data[22]),
            'notifies': int(data[23]),
            'waits': int(data[24])
        }
    
    return logfiles
        

pathlib.Path(RESULTS_PATH).mkdir(parents=True, exist_ok=True)
#open(os.path.join(RESULTS_PATH, 'results.csv'), 'w').close()

#build_and_write_results(5)

results = read_results()
create_graphs_from_results(results)
create_deps_table(results)
create_events_table(results)
create_trace_info_table(results)
create_timing_table(results)
create_vc_exceeded_table(results)