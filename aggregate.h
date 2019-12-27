#ifndef AGGREGATE_H
#define AGGREGATE_H

#include <arrow/api.h>

#include <group_by.h>
#include <print.h>
#include <util.h>
#include <cassert>
#include <tbb/enumerable_thread_specific.h>
#include <parallel.h>


// This helper is equivalent to runtime dispatch like switch((E) t) {case Args[0]:...; case Args[1]:...;...}
// except it predefines all the case branches so it is easier to support multiple instances of runtime dispatch
// over the same type & values. Example:
//   using aggregate_dispatcher = runtime_dispatcher<aggregate_task_type, count, sum, min, max, average>;
//   aggregate_dispatcher::call<template body>(dispatch_value, c, gb, &ptask);
template<typename E, E T, E... Args>
struct runtime_dispatcher {
    template<template<E> typename F, typename... A>
    static auto call(E t, A... args) -> decltype(F<T>()(args...)) {
        if(t==T)
          return F<T>()(args...);
        else if constexpr(sizeof...(Args))
          return runtime_dispatcher<E, Args...>::template call<F>(t, args...);
        assert(false);
    }
};

//++++++++++++++++++++++++++++++
// AGGREGATE
//++++++++++++++++++++++++++++++

enum aggregate_task_type { count, sum, min, max, average /*TODO median*/ };
using aggregate_dispatcher = runtime_dispatcher<aggregate_task_type, count, sum, min, max, average>;

struct aggregate_task {
  aggregate_task_type type;
  int from_column;
};

template <typename T>
struct partial_aggregate_task {
  aggregate_task *task;
  // mutable buffers?
  std::vector<T> partial;
  std::vector<T> partial1;
};

template<aggregate_task_type a, typename T>
void aggregate_internal(partial_aggregate_task<T>* p_task, int row_number, int value, int c) {
    if constexpr(a == sum) {
        p_task->partial[row_number] += value;
    }
    if constexpr(a == count) {
        p_task->partial1[row_number] += c;
    }
    if constexpr(a == min) {
        if (value < p_task->partial[row_number]) {
            p_task->partial[row_number] = value;
        }
    }
    if constexpr(a == max) {
        if (value > p_task->partial[row_number]) {
            p_task->partial[row_number] = value;
        }
    }
    if constexpr(a == average) {
        p_task->partial[row_number] += value;
        p_task->partial1[row_number] += c;
    }
}

template <typename T, typename T2, typename T4>
struct aggregate_types {
    template<aggregate_task_type A>
    struct function {
        void sequential(std::shared_ptr<T2> c, group *gb, partial_aggregate_task<T>* p_task, int chunk_number) {
            auto cv = c->raw_values();
            if (gb) {
                for (int j = 0; j < c->length(); j++) {
                    aggregate_internal<A>(p_task, gb->redirection[chunk_number][j], cv[j], 1);
                }
            } else {
                for (int j = 0; j < c->length(); j++) {
                    aggregate_internal<A>(p_task, 0, cv[j], 1);
                }
            }
        }
        std::shared_ptr<arrow::Array> finalize(partial_aggregate_task<T> &&p_task) {
            if constexpr(A == count) {
                return vector_to_array<T, T4>(p_task.partial1);
            }
            if constexpr(A == average) {
                for (int j = 0; j < p_task.partial.size(); j++) {
                    p_task.partial[j] = p_task.partial[j] / p_task.partial1[j];
                }
            }
            return vector_to_array<T, T4>(p_task.partial);
        }
        std::shared_ptr<arrow::Array> operator()(std::shared_ptr<arrow::ChunkedArray> column, group *gb, aggregate_task *task) {
            int N = 10;
            int s = gb != NULL ? gb->get_max_index() : 1;
            auto init = [task,s]{return partial_aggregate_task<T>{task, std::vector<T>(s), std::vector<T>(s)};};
            tbb::enumerable_thread_specific<partial_aggregate_task<T>> p_tasks(init); // TODO: argument initializer instead
            // for all available chunks or sequential for each incoming chunk
            parallel_for(column->num_chunks(), [&](auto j) {
                sequential(std::static_pointer_cast<T2>(column->chunk(j)), gb, &p_tasks.local(), j);
            });
            return finalize(p_tasks.combine([](auto l, auto r) { // TODO check reduction times
                  for (int row_number = 0, sz = l.partial.size(); row_number < sz; row_number++) {
                      // TODO: get rid of the second vector for functions which don't need it
                      aggregate_internal<A>(&l, row_number, r.partial[row_number], r.partial1[row_number]);
                  }
                  return l;
            }));
        }
    };
};

std::shared_ptr<arrow::Table> aggregate(std::shared_ptr<arrow::Table> table, group *gb, std::vector<aggregate_task*> tasks) {
    printf("TASK: aggregating %ld columns %s.\n", tasks.size(), gb != NULL ? "based on group_by" : "to zero column (no group_by)");
    auto begin = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<arrow::Column>> clmns;
    std::vector<std::shared_ptr<arrow::Field>> flds;
    if (gb != NULL) {
        // TODO Reserve
        clmns = std::move(gb->columns);
        flds = std::move(gb->fields);
    }
    // TBB in parallel for all available tasks TODO
    for (auto t : tasks) {
        auto column = table->column(t->from_column);
        std::shared_ptr<arrow::Array> data;
        if (column->type()->id() == arrow::Type::STRING) {
            // TODO data = aggregate_PARALLEL<std::string, arrow::StringArray, arrow::StringBuilder>(column->data(), gb, tasks[i]);
        } else if (column->type()->id() == arrow::Type::INT64) {
            using types = aggregate_types<arrow::Int64Type::c_type, arrow::Int64Array, arrow::Int64Builder>;
            data = aggregate_dispatcher::call<types::template function>(t->type, column->data(), gb, t);
        } else {
            using types = aggregate_types<arrow::DoubleType::c_type, arrow::DoubleArray, arrow::DoubleBuilder>;
            data = aggregate_dispatcher::call<types::template function>(t->type, column->data(), gb, t);
        }
        auto field = column->field();
        clmns.push_back(std::make_shared<arrow::Column>(field->name(), data));
        flds.push_back(field);
    }
    auto new_table = arrow::Table::Make(std::make_shared<arrow::Schema>(flds), clmns);
    print_time(begin);
    return new_table;
}

#endif
