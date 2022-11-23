import subprocess
import pathlib
import os
import re

RESULTS_PATH = pathlib.Path(__file__).parent.joinpath('results').joinpath(pathlib.Path(__file__).stem).resolve()
READER_PATH = pathlib.Path(__file__).parent.parent.joinpath('reader').resolve()
LOG_FILES_DIR = '/home/jan/Dev/traces/speedygo'
#LOG_FILES = ['avroraNT.log', 'batikNT.log', 'cryptNT.log', 'h2NT.log', 'jythonNT.log', 'lufactNT.log', 'lusearchNT.log', 'pmdNT.log', 'sunflowNTABORTED.log', 'tomcatFull.log', 'xalanFull.log']
LOG_FILES = ['avroraNT.log', 'batikNT.log', 'cryptNT.log', 'jythonNT.log', 'lufactNT.log', 'pmdNT.log', 'sunflowNTABORTED.log']
LOG_FILES_SPEEDYGO_FORMAT = 1

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
    args.append(fullpath)
    return subprocess.run(args, stdout=subprocess.PIPE, cwd=READER_PATH, text=True)

def build_and_write_results():
    build()
    with open(os.path.join(RESULTS_PATH, 'results.csv'), 'a') as results_file:
        for logfile in LOG_FILES:
            print("Running analysis of " + logfile)
            fullpath = os.path.join(LOG_FILES_DIR, logfile)
            
            result = run_by_detector("PWRUNDEAD3", fullpath)
            extra_edges = int(re.search(r"^Extra edges added: (\d+)$", result.stdout, flags=re.MULTILINE).group(1))
            normal_edges = int(re.search(r"^Normal edges added: (\d+)$", result.stdout, flags=re.MULTILINE).group(1))

            results_file.write(logfile + ",")
            results_file.write(str(extra_edges + normal_edges) + ",")
            results_file.write(str(extra_edges) + ",")
            results_file.write("{:.2f}".format(extra_edges / (extra_edges + normal_edges) * 100) + "\n")

pathlib.Path(RESULTS_PATH).mkdir(parents=True, exist_ok=True)
#open(os.path.join(RESULTS_PATH, 'results.csv'), 'w').close()

build_and_write_results()