import math
import pandas as pd
import matplotlib.pyplot as plt
import os
from collections import defaultdict
import seaborn as sns
import warnings
import sys
from scipy import stats
import statistics
import numpy as np



from enum import Enum

BASELINE = sys.argv[2]

fontsize_big = 25
fontsize_small = 20


class GROUP_ON(Enum):
    BENCHMARK = 0
    BRANCH = 2


pretty_name = {
    "master@63e57": "Current Renode",
    "always-go-through-softmmu-qemu-st@89867": "Renode without softmmu bypass",
    "public-hst-strong-ir@16d21": "IR strong",
    "public-hst-weak-ir@36351": "IR weak",
    "public-hst-weak-helpers@17e0b": "Helpers weak",
    "public-hst-strong-helpers@1bd00": "Helpers strong",
}

branch_colors = {
    "Current Renode": "#ad0202",
    "Renode without softmmu bypass": "#f531c7",
    "IR strong": "#0258ad",
    "Helpers strong": "#02afd6",
    "IR weak": "#117d1b",
    "Helpers weak": "#93db0d",
}

branch_order = [
    "Current Renode",
    "Renode without softmmu bypass",
    "IR strong",
    "Helpers strong",
    "IR weak",
    "Helpers weak",
]

markers = {
    "Current Renode":"D",
    "Renode without softmmu bypass": "v",
    "IR strong":"s",
    "IR weak":"P",
    "Helpers weak":"^",
    "Helpers strong":"o",
}


def read_csv_data(filepath: str) -> pd.DataFrame:
    """Reads a CSV file and returns a DataFrame."""

    df = pd.read_csv(filepath)

    if not {"real_time"}.issubset(df.columns):
        raise ValueError(f"The CSV file {filepath} must contain the required columns.")

    # Exclude potential summarization rows at end of file provided by some testing framework.
    warnings.filterwarnings(
        "ignore",
        "This pattern is interpreted as a regular expression, and has match groups. To actually get the groups, use str.extract.",
    )
    df_cleaned = df[
        ~df["name"].str.contains(r"(_mean|_median|_stddev|_cv)", regex=True)
    ]

    return df_cleaned


def group_benchmark_data(benchmark_list: list[str], group_on: GROUP_ON):
    """Reads and returns the CSV file paths grouped after benchmarks."""

    # Create a defaultdict to store lists of filepaths based on prefix
    grouped_files = defaultdict(list)

    # Iterate over filepaths and group them based on the benchmark prefix
    for file_name in benchmark_list:
        name = file_name.split("_")[group_on.value]
        cleaned_name = name.split(".")[0]  # Remove trailing .csv
        grouped_files[cleaned_name].append(file_name)
    return grouped_files


def plot_speedup_lineplot(ax, data, ylabel, title):

    # Create a figure and axis
    axes = sns.lineplot(
        ax=ax,
        data=data,
        x="threads",
        y="speedup",
        hue="pretty_branch",
        style="pretty_branch",
        palette=branch_colors,
        markers=markers,
        dashes=False,
        hue_order=branch_order,
    )

    axes.tick_params(axis="both", which="major", labelsize=fontsize_small)

    # Adding labels and title
    axes.set_xlabel("Threads", fontsize=fontsize_small)
    axes.set_ylabel(ylabel, fontsize=fontsize_small)
    axes.legend_.remove()
    axes.set_title(title, fontsize=fontsize_big)
    axes.grid(True)

    for line in axes.lines:
        line.set_markersize(12)


def plot_speedup_boxplot(ax, data, ylabel, title):

    # Create the boxplot
    axes = sns.boxplot(
        ax=ax,
        x="threads",
        y="speedup",
        data=data,
        hue="pretty_branch",
        palette=branch_colors,
        hue_order=branch_order,
    )

    axes.tick_params(axis="both", which="major", labelsize=fontsize_small)

    axes.set_xlabel("Threads", fontsize=fontsize_small)
    axes.set_ylabel(ylabel, fontsize=fontsize_small)
    axes.legend_.remove()
    axes.set_title(title, fontsize=fontsize_big)
    axes.grid(True)

    for line in axes.lines:
        line.set_markersize(12)


def process_benchmarks(
    directory: str, benchmarks: defaultdict[str, list], baseline_name: str, fake_baseline, normalize
) -> list[list[(float, float, float)]]:
    """Process benchmark files."""

    benchmarks = benchmarks.copy()

    all_data = {}  # Dictionary to store speedup data relative to the baseline files

    # Extract baseline files
    if fake_baseline: 
        baseline_files = benchmarks.pop(baseline_name) 
    else:
        baseline_files = benchmarks.get(baseline_name) 

    benchmarks = dict(sorted(benchmarks.items()))
    baseline_files.sort()

    baseline_dfs = {}  # Dictionary of cores and corresponding DataFrame

    # Create DataFrames for all baseline files and fill dictionary
    for baseline_file in baseline_files:
        nr_of_cores = baseline_file.split("_")[1]
        baseline_dfs[nr_of_cores] = read_csv_data(
            os.path.join(directory, baseline_file)
        )
        # Exclude outliers more than 2 std (~95%)
        baseline_dfs[nr_of_cores] = baseline_dfs[nr_of_cores][(np.abs(stats.zscore(baseline_dfs[nr_of_cores][['real_time']])) < 2).all(axis=1)]

    for branch, files in benchmarks.items():
        all_data[branch] = {}
        for file in files:
            nr_of_cores = file.split("_")[1]
            df = read_csv_data(os.path.join(directory, file))

            # Exclude outliers more than 2 std (~95%)
            df = df[(np.abs(stats.zscore(df[['real_time']])) < 2).all(axis=1)]


            # Calculate relative speedup compared to baseline
            if normalize:
                baseline_mean = baseline_dfs[nr_of_cores]["real_time"].mean()
                df["speedup"] = baseline_mean / df["real_time"]
            else:
                df["speedup"] = df["real_time"]

            # Calculate speedup for mean and median + standard deviation
            all_data[branch][nr_of_cores] = df

    if fake_baseline:
        # Hard-code master as a baseline
        all_data[baseline_name] = {}
        for cores in range(1,9):
            df = baseline_dfs[str(cores)]
            df["speedup"] = 1
            all_data[baseline_name][str(cores)] = df


    data_frames = []
    # Loop through each branch in the data
    for branch in all_data.keys():
        for threads in all_data[branch].keys():
            df = all_data[branch][threads]
            df["branch"] = branch
            df["threads"] = threads
            df["pretty_branch"] = df["branch"].apply(lambda name: pretty_name[name])
            data_frames.append(df)

    aggregated_data = pd.concat(data_frames)
    aggregated_data = aggregated_data.sort_values(by=["threads"])

    if baseline_files[0].startswith("UncontendedLRSC"):
        master = aggregated_data[aggregated_data["branch"] == "master@9083d"]
        ir_strong = aggregated_data[aggregated_data["branch"] == "75666-strong-hash-table-based-store-test@92619"]
        print("-" * 10 + " Strong " + "-" * 10)
        compare_helpers_vs_ir(master, ir_strong)

    return aggregated_data


def compare_helpers_vs_ir(helpers, ir):
    speedups = []
    for threads in range(1,9):
        helpers_mean = helpers[helpers["threads"] == str(threads)]["real_time"].mean()
        ir_mean = ir[ir["threads"] == str(threads)]["real_time"].mean()
        speedup = round(((helpers_mean/ir_mean)), ndigits=2)
        if (math.isnan(speedup)): continue
        speedups.append(speedup)
        print(f"ir mean for {threads} threads is {speedup} x faster")

    print(f"ir mean is on average {statistics.mean(speedups)} x faster")

def plot_speedup_and_seconds(plot_fn, data, data_normalized, benchmark, kind_name):

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(20, 9))

    plot_fn(ax1, data, ylabel="Seconds", title="(a)")
    plot_fn(ax2, data_normalized, ylabel="Relative Performance", title="(b)")

    fig.tight_layout()
    fig.suptitle(benchmark, fontsize=fontsize_big)

    # Place legend beside plot
    handles, labels = ax1.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", fontsize=fontsize_small, ncols=3)
    fig.subplots_adjust(bottom=0.22, top=0.93)

    fig.subplots_adjust(wspace=0.2)

    # Save the plot to a file
    plt.savefig(f"{kind_name}-{benchmark}.svg", format="svg")


def analyze_and_plot_speedup(benchmark_directory: str, baseline_infix: str):
    """Main function to analyze and plot speedup based on benchmark data."""

    filepaths = os.listdir(benchmark_directory)
    grouped_benchmarks = group_benchmark_data(filepaths, GROUP_ON.BENCHMARK)

    for benchmark, files in grouped_benchmarks.items():
        grouped_branches = group_benchmark_data(files, GROUP_ON.BRANCH)

        data = process_benchmarks(benchmark_directory, grouped_branches, baseline_infix, fake_baseline=False, normalize=False)
        data_normalized = process_benchmarks(benchmark_directory, grouped_branches, baseline_infix, fake_baseline=True, normalize=True)
        plot_speedup_and_seconds(plot_speedup_lineplot, data, data_normalized, benchmark, "lineplots")

        data = process_benchmarks(benchmark_directory, grouped_branches, baseline_infix, fake_baseline=False, normalize=False)
        data_normalized = process_benchmarks(benchmark_directory, grouped_branches, baseline_infix, fake_baseline=False, normalize=True)
        plot_speedup_and_seconds(plot_speedup_boxplot, data, data_normalized, benchmark, "boxplots")




def main():
    directory = sys.argv[1]
    analyze_and_plot_speedup(directory, BASELINE)


if __name__ == "__main__":
    main()
