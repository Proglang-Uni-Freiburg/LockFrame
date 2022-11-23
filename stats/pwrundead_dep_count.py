import subprocess
import pathlib
import os
import re
import matplotlib.pyplot as plt
import pandas
from cycler import cycler
import numpy as np

RESULTS_PATH = pathlib.Path(__file__).parent.joinpath('results').joinpath(pathlib.Path(__file__).stem).resolve()
READER_PATH = pathlib.Path(__file__).parent.parent.joinpath('reader').resolve()
LOG_FILES_DIR = '/home/jan/Dev/traces/speedygo'
LOG_FILES = ['avroraNT.log', 'batikNT.log', 'cryptNT.log', 'h2NT.log', 'jythonNT.log', 'lufactNT.log', 'lusearchNT.log', 'pmdNT.log', 'sunflowNTABORTED.log', 'tomcatFull.log', 'xalanFull.log']
#LOG_FILES = ['pmdNT.log']
LOG_FILES_SPEEDYGO_FORMAT = 1

def check_return_code(result):
    if result.returncode != 0:
        raise Exception(f'Error while running "{ " ".join(result.args) } "' )

def build(limit):
    print("Building limit " + str(limit) + "...")
    result = subprocess.run(['cmake', '-DCMAKE_BUILD_TYPE=Release', '-DPWRUNDEADDETECTOR_VC_PER_DEP_LIMIT=' + str(limit), '-DCOLLECT_STATISTICS=1', '.'], stdout=subprocess.PIPE, cwd=READER_PATH)
    check_return_code(result)
    result = subprocess.run(['cmake', '--build', '.'], stdout=subprocess.PIPE, cwd=READER_PATH)
    check_return_code(result)
    print("Build done\n---\n")

def run_by_detector(detector, fullpath):
    args = ['./reader', detector]
    if LOG_FILES_SPEEDYGO_FORMAT:
        args.append('--speedygo')
    args.append(fullpath)
    return subprocess.run(args, stdout=subprocess.PIPE, cwd=READER_PATH, text=True)

def build_and_write_results(limit):
    build(limit)
    with open(os.path.join(RESULTS_PATH, 'results.csv'), 'a') as results_file:
        for logfile in LOG_FILES:
            print("Collecting stats for " + logfile)
            fullpath = os.path.join(LOG_FILES_DIR, logfile)
            result = run_by_detector("PWRUNDEAD", fullpath)            
            check_return_code(result)

            undead_dependencies_sum = re.search(r"^UNDEAD dependencies sum: (\d+)$", result.stdout, flags=re.MULTILINE).group(1)
            pwrundead_dependencies_sum = re.search(r"^PWRUNDEAD dependencies sum: (\d+)$", result.stdout, flags=re.MULTILINE).group(1)
            undead_dependencies_per_thread = re.search(r"^UNDEAD dependencies per thread: ([(\d)| ]+)$", result.stdout, flags=re.MULTILINE).group(1)
            pwrundead_dependencies_per_thread = re.search(r"^PWRUNDEAD dependencies per thread: ([(\d)| ]+)$", result.stdout, flags=re.MULTILINE).group(1)

            results_file.write(str(limit) + ",")
            results_file.write(logfile + ",")
            results_file.write(undead_dependencies_sum + ",")
            results_file.write(pwrundead_dependencies_sum + ",")
            results_file.write(undead_dependencies_per_thread + ",")
            results_file.write(pwrundead_dependencies_per_thread + "\n")

def create_dep_count_graph(logfiles, filter=[]):
    limits = [1]
    for logfile in logfiles:
        for limit in logfiles[logfile]:
            if limit not in limits:
                limits.append(limit)
    limits.sort()

    result = {}
    for logfile in logfiles:
        if logfile in filter:
            continue
        result[logfile] = [logfiles[logfile][limits[1]]['undead_dependencies_sum']]
        for limit in limits:
            if limit in logfiles[logfile]:
                result[logfile].append(logfiles[logfile][limit]['pwrundead_dependencies_sum'])
    
    df = pandas.DataFrame(result, index=limits)

    ax = df.plot.line(rot=0, xticks=df.index, logx=True)
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=4, fancybox=True)
    plt.xlabel('VC Limit pro Abhängigkeit')
    plt.ylabel('Abhängigkeiten')
    plt.margins(y=0.1)
    filename_filter = ''
    if len(filter) > 0:
        filename_filter = '_filter_' + "_".join(filter)
    plt.savefig(os.path.join(RESULTS_PATH, 'results_dep_count' + filename_filter + '.png'), bbox_inches='tight')
    plt.close()

def create_boxplots(logfiles, limit):
    logfile_stats = {}
    for logfile in logfiles:
        stats = logfiles[logfile][limit]['pwrundead_dependencies_per_thread'].split(" ")
        stats = [int(x) for x in stats]
        stats_sum = sum(stats)
        stats = [x / stats_sum * 100 for x in stats]
        if len(stats) > 2:
            logfile_stats[logfile] = stats

    plt.boxplot(list(logfile_stats.values()), labels=list(logfile_stats.keys()), vert=False)
    plt.savefig(os.path.join(RESULTS_PATH, 'results_deps_per_thread_count_' + str(limit) + '.png'), bbox_inches='tight')
    plt.close()


def read_results():
    logfiles = {}
    for line in open(os.path.join(RESULTS_PATH, 'results.csv'), 'r').readlines():
        data = line.split(',')
        if data[1] not in logfiles:
            logfiles[data[1]] = {}
        logfiles[data[1]][int(data[0])] = {
            'undead_dependencies_sum': int(data[2]),
            'pwrundead_dependencies_sum': int(data[3]),
            'undead_dependencies_per_thread': data[4],
            'pwrundead_dependencies_per_thread': data[5]
        }
    
    return logfiles


pathlib.Path(RESULTS_PATH).mkdir(parents=True, exist_ok=True)
#open(os.path.join(RESULTS_PATH, 'results.csv'), 'w').close()

results = read_results()
create_dep_count_graph(results)
create_dep_count_graph(results, ['jythonNT.log'])
create_boxplots(results, 5)
create_boxplots(results, 10)
create_boxplots(results, 100)