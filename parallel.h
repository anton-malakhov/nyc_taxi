#pragma once

#if BENCH_PARALLEL_WITH_ARROW

#include <arrow/util/parallel.h>

template<typename Func>
void parallel_for(int num_tasks, Func &&f) {
  arrow::internal::ParallelFor(num_tasks, std::move(f));
}

#else

#include <tbb/tbb.h>

template<typename Func>
void parallel_for(int num_tasks, Func &&f) {
  tbb::parallel_for(0, num_tasks, f);
}

#endif