import os
import pathlib
import subprocess
import sys

READER_PATH = pathlib.Path(__file__).parent.parent.joinpath('reader').resolve()
READER_OUT_PATH = READER_PATH.parent.joinpath('out').resolve()
TRACE_FILE_PATH = pathlib.Path(__file__).parent.parent.parent.joinpath('Traces')


def find_all_trace_files():
    if not TRACE_FILE_PATH.is_dir():
        print("The supplied path is not a directory. Exiting.")
        sys.exit(1)
    trace_files = []
    for file in os.listdir(TRACE_FILE_PATH):
        if file.endswith(".std"):
            absolutePath = TRACE_FILE_PATH.joinpath(str(file)).resolve()
            trace_files.append(absolutePath)
    if len(trace_files) == 0:
        print("No trace files could be found in the supplied path. Exiting.")
        sys.exit(1)
    return trace_files


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
    traceFiles = find_all_trace_files()

    if not os.path.exists(READER_OUT_PATH):
        os.makedirs(READER_OUT_PATH)

    for filePath in traceFiles:
        args = ['./reader', '-o', READER_OUT_PATH, '--no-console', '-v', "--std", "-d PWRUNDEAD", filePath]
        process = subprocess.run(args, universal_newlines=True, text=True, cwd=READER_PATH)
        if process.returncode != 0:
            raise subprocess.CalledProcessError(process.returncode, args)

    print("Analysis has concluded.")


def main():
    build()
    run_analysis()


if __name__ == "__main__":
    main()
