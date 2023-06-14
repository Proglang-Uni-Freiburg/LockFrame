import os
import pathlib
import re
import subprocess
import time
import matplotlib.pyplot as plt
import pandas

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

# LOG_FILES_DIR = '/home/jan/Dev/traces/speedygo' LOG_FILES = ['avroraNT.log', 'batikNT.log', 'cryptNT.log',
# 'h2NT.log', 'jythonNT.log', 'lufactNT.log', 'lusearchNT.log', 'pmdNT.log', 'sunflowNTABORTED.log',
# 'tomcatFull.log', 'xalanFull.log']
LOG_FILES_SPEEDYGO_FORMAT = 0
LOG_FILES_STD_FORMAT = 1


def check_return_code(result):
    if result.returncode != 0:
        raise Exception(f'Error while running "{" ".join(result.args)} "')


def build(limit):
    print("Building...")
    result = subprocess.run(['cmake', '-DCMAKE_BUILD_TYPE=Release', '-DCOLLECT_STATISTICS=1',
                             '-DPWRUNDEADDETECTOR_VC_PER_DEP_LIMIT=' + str(limit), '.'], stdout=subprocess.PIPE,
                            cwd=READER_PATH)
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
        results_file.write(
            "trace,vc_limit,races_pwrundead,races_pwrundeadguard,time_taken_pwrundead,time_taken_pwrundeadguard,"
            "dependencies_per_thread_pwrundead,dependencies_per_thread_pwrundeadguard,"
            "possible_guard_lock_dependencies,possible_guard_locks,guard_locks_accepted,guard_locks_declined\n")
        for logfile in LOG_FILES:
            print("Running benchmark of " + logfile)
            fullpath = os.path.join(LOG_FILES_DIR, logfile)

            results_file.write(logfile + ",")
            results_file.write(str(limit) + ",")

            timeBefore = time.time_ns()
            result_pwrundead = run_by_detector("PWRUNDEAD", fullpath)
            timeTaken_pwrundead = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result_pwrundead)

            timeBefore = time.time_ns()
            result_pwrundeadguard = run_by_detector("PWRUNDEADGuard", fullpath)
            timeTaken_pwrundeadguard = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result_pwrundeadguard)

            results_file.write(
                re.search(r"^Found (\d+) races.$", result_pwrundead.stdout, flags=re.MULTILINE).group(1) + ",")
            results_file.write(
                re.search(r"^Found (\d+) races.$", result_pwrundeadguard.stdout, flags=re.MULTILINE).group(1) + ",")

            results_file.write(str(timeTaken_pwrundead) + ',')
            results_file.write(str(timeTaken_pwrundeadguard) + ',')

            results_file.write(re.search(r"^PWRUNDEAD dependencies per thread: ([(\d)| ]+)$", result_pwrundead.stdout,
                                         flags=re.MULTILINE).group(1) + ",")
            results_file.write(
                re.search(r"^PWRUNDEAD dependencies per thread: ([(\d)| ]+)$", result_pwrundeadguard.stdout,
                          flags=re.MULTILINE).group(1) + ",")

            results_file.write(re.search(r"^Possible guard lock dependencies: (\d+)$", result_pwrundeadguard.stdout,
                                         flags=re.MULTILINE).group(1) + ",")
            results_file.write(
                re.search(r"^Possible guard locks: (\d+)$", result_pwrundeadguard.stdout, flags=re.MULTILINE).group(
                    1) + ",")
            results_file.write(
                re.search(r"^Guard locks accepted: (\d+)$", result_pwrundeadguard.stdout, flags=re.MULTILINE).group(
                    1) + ",")
            results_file.write(
                re.search(r"^Guard locks declined: (\d+)$", result_pwrundeadguard.stdout, flags=re.MULTILINE).group(
                    1) + "\n")


def create_graph_time_taken(results):
    logfiles = {}
    for result in results:
        if results[result]['time_taken_pwrundead'] < 30:
            continue

        pwrundeadtime = results[result]['time_taken_pwrundead']
        pwrundeadguardtime = results[result]['time_taken_pwrundeadguard']

        logfiles[result] = pwrundeadguardtime / pwrundeadtime * 100

    df = pandas.DataFrame({
        'Logfiles': logfiles.keys(),
        'Zeit UNDEAD_PWR_GUARDS / Zeit UNDEAD_PWR': logfiles.values()
    })

    ax = df.plot.bar(x='Logfiles', y='Zeit UNDEAD_PWR_GUARDS / Zeit UNDEAD_PWR', rot=0)
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=4, fancybox=True)
    ax.set_xlabel("Logfiles")
    ax.set_ylabel("Relative Ausführungszeit [%]")
    plt.axhline(100, color=('#ff7f0e'))
    plt.setp(ax.get_xticklabels(), rotation=30, horizontalalignment='right')
    plt.margins(y=0.1)
    plt.savefig(os.path.join(RESULTS_PATH, 'time_taken_relation.png'), bbox_inches='tight')
    plt.close()


def create_graph_dependencies(results):
    logfiles = {}
    for result in results:
        if sum(results[result]['dependencies_per_thread_pwrundead']) == 0 or sum(
                results[result]['dependencies_per_thread_pwrundead']) == sum(
                results[result]['dependencies_per_thread_pwrundeadguard']):
            continue

        pwrundeaddeps = sum(results[result]['dependencies_per_thread_pwrundead'])
        pwrundeadguarddeps = sum(results[result]['dependencies_per_thread_pwrundeadguard'])

        logfiles[result] = pwrundeadguarddeps / pwrundeaddeps * 100

    df = pandas.DataFrame({
        'Logfiles': logfiles.keys(),
        'Deps UNDEAD_PWR_GUARDS / Deps UNDEAD_PWR': logfiles.values()
    })

    ax = df.plot.bar(x='Logfiles', y='Deps UNDEAD_PWR_GUARDS / Deps UNDEAD_PWR', rot=0)
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=4, fancybox=True)
    ax.set_xlabel("Logfiles")
    ax.set_ylabel("Relative Anzahl Dependencies [%]")
    plt.axhline(100, color=('#ff7f0e'))
    plt.setp(ax.get_xticklabels(), rotation=30, horizontalalignment='right')
    plt.margins(y=0.1)
    plt.savefig(os.path.join(RESULTS_PATH, 'relative_dependencies.png'), bbox_inches='tight')
    plt.close()


def create_graph_accepted(results):
    logfiles = {}
    for result in results:
        if results[result]['possible_guard_locks'] <= 0:
            continue

        accepted = results[result]['guard_locks_accepted']
        all = results[result]['possible_guard_locks']

        logfiles[result] = accepted / all * 100

    df = pandas.DataFrame({
        'Logfiles': logfiles.keys(),
        'Guard Locks übernommen': logfiles.values()
    })

    ax = df.plot.bar(x='Logfiles', y='Guard Locks übernommen', rot=0)
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=4, fancybox=True)
    ax.set_xlabel("Logfiles")
    ax.set_ylabel("Übernommene Guard Locks [%]")
    plt.axhline(100, color=('#ff7f0e'))
    plt.setp(ax.get_xticklabels(), rotation=30, horizontalalignment='right')
    plt.margins(y=0.1)
    plt.savefig(os.path.join(RESULTS_PATH, 'relative_accepted_guard_locks.png'), bbox_inches='tight')
    plt.close()


def create_graphs_from_results(results):
    create_graph_time_taken(results)
    create_graph_dependencies(results)
    create_graph_accepted(results)


def create_deps_table(results):
    with open(os.path.join(RESULTS_PATH, 'deps_table.csv'), 'w') as results_file:
        results_file.write(
            'trace,possible_guard_lock_dependencies,possible_guard_locks,guard_locks_accepted,guard_locks_declined\n')
        for trace in results:
            if results[trace]['possible_guard_lock_dependencies'] == 0:
                continue

            results_file.write(trace + ',')
            results_file.write(str(results[trace]['possible_guard_lock_dependencies']) + ',')
            results_file.write(str(results[trace]['possible_guard_locks']) + ',')
            results_file.write(str(results[trace]['guard_locks_accepted']) + ',')
            results_file.write(str(results[trace]['guard_locks_declined']) + '\n')


def create_time_taken_table(results):
    with open(os.path.join(RESULTS_PATH, 'time_taken_table.csv'), 'w') as results_file:
        results_file.write('trace,time_taken_pwrundead,time_taken_pwrundeadguard\n')
        for trace in results:
            if (results[trace]['time_taken_pwrundead'] < 30):
                continue
            results_file.write(trace + ',')
            results_file.write(str(results[trace]['time_taken_pwrundead']) + ',')
            results_file.write(str(results[trace]['time_taken_pwrundeadguard']) + '\n')


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
            'races_pwrundead': int(data[2]),
            'races_pwrundeadguard': int(data[3]),
            'time_taken_pwrundead': int(data[4]),
            'time_taken_pwrundeadguard': int(data[5]),
            'dependencies_per_thread_pwrundead': [int(x) for x in data[6].split(' ')],
            'dependencies_per_thread_pwrundeadguard': [int(x) for x in data[7].split(' ')],
            'possible_guard_lock_dependencies': int(data[8]),
            'possible_guard_locks': int(data[9]),
            'guard_locks_accepted': int(data[10]),
            'guard_locks_declined': int(data[11])
        }

    return logfiles


pathlib.Path(RESULTS_PATH).mkdir(parents=True, exist_ok=True)
# open(os.path.join(RESULTS_PATH, 'results.csv'), 'w').close()

# build_and_write_results(5)

results = read_results()
create_graphs_from_results(results)
create_deps_table(results)
create_time_taken_table(results)
