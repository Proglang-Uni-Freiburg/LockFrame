import glob
import os
import pathlib
import subprocess
import sys
import matplotlib.pyplot as plt
import pandas

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
        args = ['./reader', '-o', READER_OUT_PATH, '--no-console', '-v', "--std", "--csv", "-d PWROptimized4", filePath]
        process = subprocess.run(args, universal_newlines=True, text=True, cwd=READER_PATH)
        if process.returncode != 0:
            raise subprocess.CalledProcessError(process.returncode, args)

    print("Analysis has concluded.")


def collect_and_plot_results():
    statResultsList = glob.glob(str(READER_OUT_PATH) + "/PWROptimized4_STATS_*")
    originalStats = {}
    eventKeys = ["reads", "writes", "acquires", "releases", "forks", "joins", "notify", "notifywait"]
    matplotData = {key: [] for key in eventKeys}

    for foundFilepath in statResultsList:
        traceName = os.path.basename(foundFilepath).split("_")[-1].replace(".std.csv", "")
        originalStats[traceName] = {}
        with open(foundFilepath) as statsFile:
            for line in statsFile:
                stat = line.split(",")
                originalStats[traceName][stat[0]] = stat[1].strip()

    for traceName in originalStats.keys():
        traceStats = originalStats[traceName]
        # Making use of python's Dictionary comprehensions to filter out the keys we want.
        filteredStats = {key: int(traceStats[key]) for key in eventKeys}
        # Make the values percentage based.
        totalEvents = sum(filteredStats.values())
        for key, value in filteredStats.items():
            matplotData[key].append(value / totalEvents * 100)

    df = pandas.DataFrame(matplotData, index=originalStats.keys())
    ax = df.plot.barh(stacked=True, rot=0)
    ax.legend(loc='upper center', bbox_to_anchor=(
        0.5, 1.2), ncol=4, fancybox=True)
    ax.set_ylabel("[%]", loc="top", rotation="horizontal")
    ax.yaxis.set_label_coords(1, -0.065)
    plt.margins(y=0.1)
    plt.subplots_adjust(top=0.82, left=0.2)
    plt.savefig(pathlib.Path(__file__).parent.joinpath('eventtypes.png'), bbox_inches='tight')
    plt.close()



def main():
    # build()
    # run_analysis()
    collect_and_plot_results()


if __name__ == "__main__":
    main()
