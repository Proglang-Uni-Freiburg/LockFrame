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
        results_file.write("trace,vc_limit,time_taken_undead,time_taken_pwrundead,time_taken_pwrundeadguard,pwrundead_deps,pwrundeadguard_deps,deps_diff,ls_size_diff,deps_with_additional_guards,additional_deps\n")
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
            result_pwrundeadguard = run_by_detector("PWRUNDEADGuard", fullpath)
            timeTaken_pwrundeadguard = (time.time_ns() - timeBefore) // 1000 // 1000
            check_return_code(result_pwrundeadguard)

            noguard_undead_deps = re.search(r"^UNDEAD dependencies sum: (\d+)$", result_pwrundead.stdout, flags=re.MULTILINE).group(1)
            noguard_undead_ls_size = re.search(r"^UNDEAD size of all locksets: (\d+)$", result_pwrundead.stdout, flags=re.MULTILINE).group(1)

            guard_undead_deps = re.search(r"^UNDEAD dependencies sum: (\d+)$", result_pwrundeadguard.stdout, flags=re.MULTILINE).group(1)
            guard_undead_ls_size = re.search(r"^UNDEAD size of all locksets: (\d+)$", result_pwrundeadguard.stdout, flags=re.MULTILINE).group(1)

            deps_with_additional_guards = re.search(r"^UNDEAD dependencies with additional guards: (\d+)$", result_pwrundeadguard.stdout, flags=re.MULTILINE).group(1)
            additional_deps = re.search(r"^UNDEAD guard only dependencies: (\d+)$", result_pwrundeadguard.stdout, flags=re.MULTILINE).group(1)

            undead_deps_diff = int(guard_undead_deps) - int(noguard_undead_deps)
            undead_ls_size_diff = int(guard_undead_ls_size) - int(noguard_undead_ls_size)

            results_file.write(str(timeTaken_undead) + ",")
            results_file.write(str(timeTaken_pwrundead) + ",")
            results_file.write(str(timeTaken_pwrundeadguard) + ",")
            results_file.write(str(noguard_undead_deps) + ",")
            results_file.write(str(guard_undead_deps) + ",")
            results_file.write(str(undead_deps_diff) + ",")
            results_file.write(str(undead_ls_size_diff) + ",")
            results_file.write(str(deps_with_additional_guards) + ",")
            results_file.write(str(additional_deps) + "\n")
        

pathlib.Path(RESULTS_PATH).mkdir(parents=True, exist_ok=True)
#open(os.path.join(RESULTS_PATH, 'results.csv'), 'w').close()

build_and_write_results(5)