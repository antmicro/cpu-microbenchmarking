#!/usr/bin/env bash
set -eu

. make.sh

CORES_LIST=$(seq 1 8)

BENCHMARK_NAMES=(
    "ContendedLRSC"
    "UncontendedLRSC"
)

for tlib_branch_ref in ${tlib_branch_refs[@]}; do 
    tlib_folder="tlib_$tlib_branch_ref"
    bench_executable="./cpu-benchmark_${tlib_branch_ref}"

    echo tlib dir: ${tlib_folder}

    for cores in ${CORES_LIST[@]}; do 
    for benchmark_name in ${BENCHMARK_NAMES[@]}; do 
        output_name="${benchmark_name}_${cores}_${tlib_branch_ref}.csv"

        if [ "$(uname)" = "Linux" ]; then
            LD_LIBRARY_PATH="${tlib_folder}/build" \
            ${bench_executable} \
            --benchmark_out="${output_name}" \
            --benchmark_out_format=csv \
            --benchmark_filter=${benchmark_name}/${cores} \
            ${@:2}
        else
            DYLD_LIBRARY_PATH="${tlib_folder}/build:" ${bench_executable} \
            --benchmark_out="${output_name}" \
            --benchmark_out_format=csv \
            --benchmark_filter=${benchmark_name}/${cores} \
            ${@:2}
        fi

        #
        # Google Benchmark outputs CSVs with a non-CSV header containing system info.
        # We need to remove this for compatibility with CSV-parsing tools.
        #

        # Get line number of actual csv header.
        csv_header_line=$(awk '/name,iterations,real_time,/{ print NR; exit }' "${output_name}")
        lines_to_remove=$((csv_header_line - 1))
        # Delete lines up to csv header.
        sed -i 1,${lines_to_remove}d "${output_name}"
    done
    done
done
