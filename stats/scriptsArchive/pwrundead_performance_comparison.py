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
    with open(os.path.join(RESULTS_PATH, 'results.csv'), 'a') as results_file:
        results_file.write("trace,vc_limit,time_taken_full_undead,time_taken_full_undead_pwr,time_taken_full_undead_pwr_extra,time_taken_phase2_undead,time_taken_phase2_undead_pwr,time_taken_phase2_undead_pwr_extra\n")
        for logfile in LOG_FILES:
            print("Running benchmark of " + logfile)
            fullpath = os.path.join(LOG_FILES_DIR, logfile)

            timeBefore_undead = time.time_ns()
            result = run_by_detector("UNDEAD", fullpath)
            timeTaken_undead = (time.time_ns() - timeBefore_undead) // 1000 // 1000
            timeTaken2_undead = re.search(r"^Phase 2 elapsed time in milliseconds: (\d+)$", result.stdout, flags=re.MULTILINE).group(1)
            check_return_code(result)

            timeBefore_undead_pwr = time.time_ns()
            result = run_by_detector("PWRUNDEAD", fullpath)
            timeTaken_undead_pwr = (time.time_ns() - timeBefore_undead_pwr) // 1000 // 1000
            timeTaken2_undead_pwr = re.search(r"^Phase 2 elapsed time in milliseconds: (\d+)$", result.stdout, flags=re.MULTILINE).group(1)
            check_return_code(result)

            timeBefore_undead_pwr_extra = time.time_ns()
            result = run_by_detector("PWRUNDEAD3", fullpath)
            timeTaken_undead_pwr_extra = (time.time_ns() - timeBefore_undead_pwr_extra) // 1000 // 1000
            timeTaken2_undead_pwr_extra = re.search(r"^Phase 2 elapsed time in milliseconds: (\d+)$", result.stdout, flags=re.MULTILINE).group(1)
            check_return_code(result)

            results_file.write(logfile + ",")
            results_file.write(str(limit) + ",")
            results_file.write(str(timeTaken_undead) + ",")
            results_file.write(str(timeTaken_undead_pwr) + ",")
            results_file.write(str(timeTaken_undead_pwr_extra) + ",")
            results_file.write(str(timeTaken2_undead) + ",")
            results_file.write(str(timeTaken2_undead_pwr) + ",")
            results_file.write(str(timeTaken2_undead_pwr_extra) + "\n")

pathlib.Path(RESULTS_PATH).mkdir(parents=True, exist_ok=True)
#open(os.path.join(RESULTS_PATH, 'results.csv'), 'w').close()

build_and_write_results(5)