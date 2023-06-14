import subprocess
import pathlib
import os
import time
import pandas
import matplotlib.pyplot as plt
from cycler import cycler
import numpy as np

RESULTS_PATH = pathlib.Path(__file__).parent.joinpath('results').joinpath(pathlib.Path(__file__).stem).resolve()
READER_PATH = pathlib.Path(__file__).parent.parent.joinpath('reader').resolve()
LOG_FILES_DIR = '/home/jan/Dev/traces/speedygo'
LOG_FILES = ['avroraNT.log', 'batikNT.log', 'cryptNT.log', 'h2NT.log', 'jythonNT.log', 'lufactNT.log', 'lusearchNT.log', 'pmdNT.log', 'sunflowNTABORTED.log', 'tomcatFull.log', 'xalanFull.log']
LOG_FILES_SPEEDYGO_FORMAT = 1

def check_return_code(result):
    if result.returncode != 0:
        raise Exception(f'Error while running "{ " ".join(result.args) } "' )

def build_limit(limit):
    print("Building with limit " + str(limit) + "...")
    result = subprocess.run(['cmake', '-DCMAKE_BUILD_TYPE=Release', '-DPWRUNDEADDETECTOR_VC_PER_DEP_LIMIT=' + str(limit), '.'], stdout=subprocess.PIPE, cwd=READER_PATH)
    check_return_code(result)
    result = subprocess.run(['cmake', '--build', '.'], stdout=subprocess.PIPE, cwd=READER_PATH)
    check_return_code(result)
    print("Build done\n---\n")

def build_and_write_results(limit):
    build_limit(limit)
    with open(os.path.join(RESULTS_PATH, 'results.csv'), 'a') as results_file:
        for logfile in LOG_FILES:
            print("Running benchmark of " + logfile)
            fullpath = os.path.join(LOG_FILES_DIR, logfile)
            timeBefore = time.time_ns()
            args = ['./reader', 'PWRUNDEAD']
            if LOG_FILES_SPEEDYGO_FORMAT:
                args.append('--speedygo')
            args.append(fullpath)
            result = subprocess.run(args, stdout=subprocess.PIPE, cwd=READER_PATH)
            timeTaken = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result)
            results_file.write(logfile + ',')
            results_file.write(str(limit) + ',')
            results_file.write(str(timeTaken) + '\n')

def create_graph_from_results():
    logfiles = {}
    limits = []
    for line in open(os.path.join(RESULTS_PATH, 'results.csv'), 'r').readlines():
        data = line.split(',')
        if data[0] not in logfiles:
            logfiles[data[0]] = {}
        logfiles[data[0]][int(data[1])] = int(data[2])
        if int(data[1]) not in limits:
            limits.append(int(data[1]))
    limits.sort()
    
    result = {}
    for logfile in logfiles:
        result[logfile] = []
        for limit in limits:
            result[logfile].append(logfiles[logfile][limit])
    
    df = pandas.DataFrame(result, index=limits)

    print(result)
    print(limits)

    ax = df.plot.line(rot=0, xticks=df.index, logx=True)
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=4, fancybox=True)
    plt.xlabel('VC Limit pro Abhängigkeit')
    plt.ylabel('Laufzeit [ms]')
    plt.margins(y=0.1)
    plt.savefig(os.path.join(RESULTS_PATH, 'results.png'), bbox_inches='tight')


pathlib.Path(RESULTS_PATH).mkdir(parents=True, exist_ok=True)
#open(os.path.join(RESULTS_PATH, 'results.csv'), 'w').close()

#build_and_write_results(1)
#build_and_write_results(2)
#build_and_write_results(3)
#build_and_write_results(5)
#build_and_write_results(10)
#build_and_write_results(20)
#build_and_write_results(50)
#build_and_write_results(100)
#build_and_write_results(1000)

create_graph_from_results()