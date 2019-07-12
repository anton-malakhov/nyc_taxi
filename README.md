# 1B rides NYC taxi native benchmark

This repository reflects progress of performance and design study for [1B ride NYC taxi benchamarks](https://tech.marksblogg.com/benchmarks.html).

# Compilation

[![Build Status](https://travis-ci.org/anton-malakhov/nyc_taxi.svg?branch=master)](https://travis-ci.org/anton-malakhov/nyc_taxi)

1. Install and activate [miniconda](https://docs.conda.io/en/latest/miniconda.html)
2. Create development environment: `conda env create -n arrow-def -f conda/default-env.yml`
3. Activate this environment: `conda activate arrow-def`
4. Configure: `cmake -GNinja -DCMAKE_BUILD_TYPE=release -S . -B ../build`
5. Build: `cmake --build ../build`
6. Allocate huge memory pages: `sudo bash -c 'echo 30000 > /proc/sys/vm/nr_hugepages'`
7. Download [trips.xaa.csv.gz](https://aws159-usea1-1ltzu05bg2g89.s3.amazonaws.com/trips_xaa.csv.gz), `gzip -d` it
8. Run `../build/bench_all` for a single run of all execution nodes.
9. Adjust and run `bash run-aff-bsz.sh` for collecting times of CSV reader in different modes.
