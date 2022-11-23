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
LOG_FILES_DIR = '/home/jan/Dev/traces/decapo'
#LOG_FILES = ['bufwriter.std', 'derby.std', 'ftpserver.std', 'jigsaw.std', 'linkedlist.std', 'lufact.std', 'moldyn.std', 'raytracer.std', 'readerswriters.std', 'sunflow.std']
LOG_FILES = ['luindex.std', 'lusearch.std', 'sor.std', 'tsp.std']
LOG_FILES_TOO_SMALL = ['account.std', 'airlinetickets.std', 'array.std', 'boundedbuffer.std', 'bubblesort.std', 'clean.std', 'critical.std', 'lang.std', 'mergesort.std', 'pingpong.std', 'producerconsumer.std', 'twostage.std', 'wronglock.std']
LOG_FILES_BIG = ['batik.std', 'cryptorsa.std', 'luindex.std', 'lusearch.std', 'sor.std', 'tsp.std', 'xalan.std']
#LOG_FILES_DIR = '/home/jan/Dev/traces/speedygo'
#LOG_FILES = ['avroraNT.log', 'batikNT.log', 'cryptNT.log', 'h2NT.log', 'jythonNT.log', 'lufactNT.log', 'lusearchNT.log', 'pmdNT.log', 'sunflowNTABORTED.log', 'tomcatFull.log', 'xalanFull.log']
#LOG_FILES = ['avroraNT.log', 'batikNT.log', 'cryptNT.log', 'jythonNT.log', 'lufactNT.log', 'pmdNT.log', 'sunflowNTABORTED.log']
LOG_FILES_SPEEDYGO_FORMAT = 0
LOG_FILES_STD_FORMAT = 1

def check_return_code(result):
    if result.returncode != 0:
        raise Exception(f'Error while running "{ " ".join(result.args) } "' )

def build():
    print("Building...")
    result = subprocess.run(['cmake', '-DCMAKE_BUILD_TYPE=Release', '-DPWRUNDEADDETECTOR_VC_PER_DEP_LIMIT=' + str(5), '.'], stdout=subprocess.PIPE, cwd=READER_PATH)
    check_return_code(result)
    result = subprocess.run(['cmake', '--build', '.'], stdout=subprocess.PIPE, cwd=READER_PATH)
    check_return_code(result)
    print("Build done\n---\n")

def run_by_detector(detector, fullpath):
    args = ['./reader', detector]
    if LOG_FILES_SPEEDYGO_FORMAT:
        args.append('--speedygo')
    elif LOG_FILES_STD_FORMAT:
        args.append('--std')
    args.append(fullpath)
    return subprocess.run(args, stdout=subprocess.PIPE, cwd=READER_PATH)

def build_and_write_results():
    build()
    with open(os.path.join(RESULTS_PATH, 'results.csv'), 'a') as results_file:
        for logfile in LOG_FILES:
            print("Running benchmark of " + logfile)
            fullpath = os.path.join(LOG_FILES_DIR, logfile)

            print("Undead...")
            timeBefore = time.time_ns()
            result = run_by_detector("UNDEAD", fullpath)            
            timeTaken = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result)
            results_file.write(logfile + ',')
            results_file.write(str(timeTaken) + ',')

            print("PWRUNDEAD...")
            timeBefore = time.time_ns()
            result = run_by_detector("PWRUNDEAD", fullpath)
            timeTaken = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result)
            results_file.write(str(timeTaken) + ',')
            
            print("PWRUNDEAD3...")
            timeBefore = time.time_ns()
            result = run_by_detector("PWRUNDEAD3", fullpath)
            timeTaken = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result)
            results_file.write(str(timeTaken) + '\n')


def create_graph_from_results():
    logfiles = {}
    for line in open(os.path.join(RESULTS_PATH, 'results.csv'), 'r').readlines():
        data = line.split(',')
        if data[0] not in logfiles:
            logfiles[data[0]] = {}
        fullTime = int(data[3])
        undeadTime = int(data[1]) / fullTime * 100
        pwroverheadTime = (int(data[2]) / fullTime * 100) - undeadTime
        extraedgesoverheadTime = 100 - undeadTime - pwroverheadTime
        logfiles[data[0]]['undead'] = undeadTime
        logfiles[data[0]]['pwroverhead'] = pwroverheadTime
        logfiles[data[0]]['extraedgesoverhead'] = extraedgesoverheadTime

    df = pandas.DataFrame({
        'UNDEAD': [logfiles[x]['undead'] for x in logfiles],
        'PWR overhead': [logfiles[x]['pwroverhead'] for x in logfiles],
        'Extra edges overhead': [logfiles[x]['extraedgesoverhead'] for x in logfiles],
    }, index=[x for x in logfiles])

    ax = df.plot.bar(stacked=True)
    ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1), ncol=4, fancybox=True)
    ax.set_xlabel("Logfiles")
    ax.set_ylabel("Ausführungszeit in [%]")
    plt.setp(ax.get_xticklabels(), rotation=30, horizontalalignment='right')
    plt.margins(y=0.1)
    plt.savefig(os.path.join(RESULTS_PATH, 'results.png'), bbox_inches='tight')


pathlib.Path(RESULTS_PATH).mkdir(parents=True, exist_ok=True)
#open(os.path.join(RESULTS_PATH, 'results.csv'), 'w').close()

build_and_write_results()

create_graph_from_results()