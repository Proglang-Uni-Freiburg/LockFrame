import os
import pathlib
import subprocess

READER_PATH = pathlib.Path(__file__).parent.parent.joinpath('reader').resolve()
READER_OUT_PATH = READER_PATH.parent.joinpath('out').resolve()
TRACE_FILE_PATH = pathlib.Path(__file__).parent.parent.parent.joinpath('Traces') \
    .joinpath('zero-reversal-logs/final-logs/sunflow/sunflow.std').resolve()


def build():
    print("Building reader...")
    result = subprocess.run(['cmake', '-DCMAKE_BUILD_TYPE=Release', '-DCOLLECT_STATISTICS=1',
                             '-DPWRUNDEADDETECTOR_VC_PER_DEP_LIMIT=5', '.'],
                            cwd=READER_PATH)
    if result.returncode != 0:
        raise Exception('Compile step failed')
    result = subprocess.run(['cmake', '--build', '.'], cwd=READER_PATH)
    if result.returncode != 0:
        raise Exception('Build step failed')
    print("Build finished.")


def run_analysis():
    print("Commencing analysis...")
    if not os.path.exists(READER_OUT_PATH):
        os.makedirs(READER_OUT_PATH)
    args = ['./reader', '-o', READER_OUT_PATH, '--no-console', '-v', "--std", "-d PWRUNDEAD", TRACE_FILE_PATH]
    process = subprocess.run(args, universal_newlines=True, text=True, cwd=READER_PATH)
    if process.returncode != 0:
        raise subprocess.CalledProcessError(process.returncode, args)
    print("Analysis has concluded.")


def main():
    build()
    run_analysis()


if __name__ == "__main__":
    main()
