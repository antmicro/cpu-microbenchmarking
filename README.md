# CPU Microbenchmarks
Copyright (c) 2024-2025 [Antmicro](https://www.antmicro.com)

This repository allows for microbenchmarking [Renode](https://github.com/renode/renode) CPU implementation.

## Dependencies

You'll need [Google Benchmark](https://github.com/google/benchmark) to build these microbenchmarks. To install it, follow these steps:
```bash
git clone https://github.com/google/benchmark.git

# Build google benchmark
cd benchmark
cmake -E make_directory "build"
cmake -E chdir "build" cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release ../
cmake --build "build" --config Release -j$(nproc)

# Install globally
sudo cmake --build "build" --config Release --target install
```

You'll also need `build-essential`, `cmake` and `libelf-dev`, installed e.g. by `apt install build-essential cmake libelf-dev`.

## Usage

To get the benchmark binary, call `setup-benchmark.sh` to clone and build the relevant branches.

Then, use `run.sh` to execute the benchmarks.
Output will be generated in the repository directory as .csv files.

If you make changes to the benchmarking code and want to recompile, use `make.sh`.

The scripts accept a positional parameter, which is a regex string used to match which branch to use from `config.sh`.
If none is provided, all are used.

For example, if you'd like to benchmark all configured branches then run:
```bash
./run.sh
```
and if you'd like to see that all benchmarks work correctly, dry-run them:
```bash
./run.sh ".*" --benchmark_dry_run
```
or if you want to benchmark a specific branch:
```bash
./run.sh "public-hst-weak-ir@.*"
```

The `run.sh` script passes additional positional parameters to the benchmark binary.
Use `run.sh ".*" --help` for a list of options (the standard [Google Benchmark options](https://github.com/google/benchmark/blob/main/docs/user_guide.md#command-line)).

Once done, artifacts can be cleaned up using `clean.sh`.

Figures can be generated using `python3 benchmark-plot.py "path/to/result-csvs/" "baseline@short_sha"`
