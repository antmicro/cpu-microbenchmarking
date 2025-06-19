#!/usr/bin/env bash
set -eu

# Import configuration
. config.sh

branch_regex=${1:-".*"}

tlib_branch_refs=$(
    for ref in ${refs[@]}; do 
        sha="${ref#*@}" 
        sha_short=$(git rev-parse --short=5 ${sha})
        tlib_branch_name="${ref%@*}" # Assuming branches are named the same in every submodule.
        fully_qualified_branch="${tlib_branch_name}@${sha_short}"

        if [[ $fully_qualified_branch =~ $branch_regex ]]; then 
            echo $fully_qualified_branch
        else 
            continue
        fi
    done
)

if [ $0 == "./make.sh" ] ; then
    
    for tlib_branch_ref in ${tlib_branch_refs[@]}; do 
        echo building $tlib_branch_ref

        tlib_folder="tlib_$tlib_branch_ref"
        bench_executable="./cpu-benchmark_$tlib_branch_ref"

        if [ ! -e ${tlib_folder}/build/libtlib.${SHLIB_EXT:-so} ] ; then
        >&2 echo error: libtlib.${SHLIB_EXT:-so} not found!
        exit 1
        fi

        if [[ $tlib_branch_ref = *ir* ]] ; then
            RESIZABLE_STORE_TABLE="-DRESIZABLE_STORE_TABLE=1"
        else
            RESIZABLE_STORE_TABLE=""
        fi

        # Compile c files
        gcc *.c -c -O3 -g3 -I. -I ./$tlib_folder/include ${RESIZABLE_STORE_TABLE} # -fsanitize=address -fno-omit-frame-pointer
        # Compile c++ files
        g++ cpu-benchmark.cc -c -O3 -g3 -std=c++17  ${RESIZABLE_STORE_TABLE} -I ./$tlib_folder/include -o cpu-benchmark.o  # -fsanitize=address -fno-omit-frame-pointer
        # Link everything together 
        g++ -o ${bench_executable} *.o  ${RESIZABLE_STORE_TABLE} -isystem benchmark/include -Lbenchmark/build/src -lbenchmark -L${tlib_folder}/build -L${tlib_folder}/build/tcg/src/tcglib-build/ -ltlib ${LIBELF:-"-lelf"} -lz -pthread # -fsanitize=address

    done

fi

