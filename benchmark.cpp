#include <arrow/api.h>
#include <arrow/util/thread_pool.h>

#include <aggregate.h>
#include <load.h>
#include <group_by.h>
#include <print.h>
#include <transform.h>
#include <sort.h>
#include <timegm.h>

#include <cmath>

// sudo apt-get install libtbb-dev
// gcc benchmark.cpp -O3 -I. -I../arrow/cpp/src/ -I../arrow/cpp/release/src/ -L../arrow/cpp/release/release/ -Wl,-rpath=../arrow/cpp/release/release/ -larrow -lstdc++ -lm -ltbb -std=c++17
// gcc benchmark.cpp -O3 -I. -Wl,-rpath=../arrow/cpp/release/release/ -larrow -lstdc++ -lm -ltbb -std=c++17

//++++++++++++++++++++++++++++++
// BENCHMARK
//++++++++++++++++++++++++++++++
std::shared_ptr<arrow::Table>
taxi1(std::shared_ptr<arrow::Table> table) {
    //SELECT cab_type, count(cab_type)
    //FROM trips
    //GROUP BY cab_type;
    printf("NAME: Taxi number 1\n");
    group *taxi1_group_by = group_by(table, {24});
    aggregate_task taxi1_task = {count, 0};
    return aggregate(table, taxi1_group_by, {&taxi1_task});
}

std::shared_ptr<arrow::Table>
taxi2(std::shared_ptr<arrow::Table> table) {
    //SELECT passenger_count, avg(total_amount)
    //FROM trips
    //GROUP BY passenger_count;
    printf("NAME: Taxi number 2\n");
    group *taxi2_group_by = group_by(table, {10});
    aggregate_task taxi2_task = {average, 19};
    return aggregate(table, taxi2_group_by, {&taxi2_task});
}

std::shared_ptr<arrow::Table>
taxi3(std::shared_ptr<arrow::Table> table) {
    //SELECT passenger_count,
    //    EXTRACT(year from pickup_datetime) as year,
    //    count(*)
    //FROM trips
    //GROUP BY passenger_count,
    //    year;
    printf("NAME: Taxi number 3\n");
    auto year = [](int64_t time) {
        time_t tt = static_cast<time_t>(time); struct tm r;
        return int64_t(_der_gmtime(tt, &r)->tm_year + 1900); }; // gmtime (not localtime) because of python
    auto taxi3_table = transform<int64_t, int64_t, arrow::TimestampArray, arrow::Int64Builder>(table, 2, year);
    group *taxi3_group_by = group_by(taxi3_table, {2, 10});
    aggregate_task taxi3_task = {count, 0};
    auto a = aggregate(taxi3_table, taxi3_group_by, {&taxi3_task});
  return sort(a, {0, 1}, {asc, asc}, flat); // Only one chunk for sort here, not a good checking - see sortAll
}

std::shared_ptr<arrow::Table>
taxi4(std::shared_ptr<arrow::Table> table) {
    //SELECT passenger_count,
    //    EXTRACT(year from pickup_datetime) as year,
    //    round(trip_distance) distance,
    //    count(*) trips
    //FROM trips
    //GROUP BY passenger_count,
    //    year,
    //    distance
    //ORDER BY year,
    //    trips desc;
    printf("NAME: Taxi number 4\n");
    auto year = [](int64_t time) {
        time_t tt = static_cast<time_t>(time); struct tm r;
        return int64_t(_der_gmtime(tt, &r)->tm_year + 1900); }; // gmtime (not localtime) because of python
    auto taxi4_table = transform<int64_t, int64_t, arrow::TimestampArray, arrow::Int64Builder>(table, 2, year);
    auto taxi4_table1 = transform<double, double, arrow::DoubleArray, arrow::DoubleBuilder>(taxi4_table, 11, round);
    group *taxi4_group_by = group_by(taxi4_table1, {2, 10, 11});
    aggregate_task taxi4_task = {count, 0};
    auto taxi4_table2 = aggregate(taxi4_table1, taxi4_group_by, {&taxi4_task});
    // numbers of columns are completely different here
    return sort(taxi4_table2, {0, 1, 2, 3}, {asc, asc, asc, desc}, flat); // Only one chunk for sort here, not a good checking - see sortAll
}

void perf(std::shared_ptr<arrow::Table> table) {
    printf("NAME: perf: aggregate, sort single and sort multiple for whole table\n");
    // TODO we can build only double, int64 and string columns currently, so we can't sort all our table

    aggregate_task max_passengers = {max, 10};
    print_table(aggregate(table, NULL, {&max_passengers}));

    std::vector<std::shared_ptr<arrow::Column>> clmns;
    std::vector<std::shared_ptr<arrow::Field>> flds;
    clmns.push_back(table->column(24));
    flds.push_back(table->column(24)->field());
    clmns.push_back(table->column(10));
    flds.push_back(table->column(10)->field());
    clmns.push_back(table->column(11));
    flds.push_back(table->column(11)->field());
    clmns.push_back(table->column(19));
    flds.push_back(table->column(19)->field());
    auto reduced_table = arrow::Table::Make(std::make_shared<arrow::Schema>(flds), clmns);

    auto single = sort(reduced_table, {3}, {desc}, tree);
    auto multiple = sort(reduced_table, {1, 0}, {asc, desc}, tree);

    print_table(single);
    print_table(multiple);
}

int main(int argc, char** argv) {

    if(argc>2) arrow::SetCpuThreadPoolCapacity(atoi(argv[2]));
    int bsz = 1024*1024;
    if(argc>3) bsz = atoi(argv[3]);
    printf("Thread number: %d; Block size: %d\n", arrow::GetCpuThreadPoolCapacity(), bsz);

    auto table = load_csv(argc > 1 ? argv[1] : "trips_xaa.csv", true, bsz);

    //  2 - pickup_datetime
    // 10 - passenger count
    // 11 - trip distance
    // 19 - total amount
    // 24 - cab type
#ifndef BENCH_CSV_ONLY
    //print_table(
    taxi1(table);//);
    //print_table(
    taxi2(table);//);
    //print_table(
    taxi3(table);//);
    //print_table(
    taxi4(table);//);
    //perf(table);
#endif

    return 0;
}

/* TODO peephole optimizations:
    count can be done inside group_by by request
    average can be also used for sum and count
    try group_by with predefined hashes
    inline

   TODO stability:
    handling nil values
    change all C pointers to shared pointers
    check where pointers can be changes to references
    memory leaks?

   TODO features:
    transformation between multiple columns, do we need it?
    transformation multiple columns in one function
    read csv with custom header
    transform without templates - how to determine functions?
    aggregate - median

   TODO quality:
    single thread load_csv to parameters
    readme
    error checking via returning status
    sort is buggy

   TODO assumptions:
    all columns have the same number (and corresponding length) of chunks
    first parallelization step will be with record batch size equal to chunk
*/
