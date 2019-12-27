cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

project(bench CXX)

set(CMAKE_CXX_FLAGS "-Wno-sign-compare -Wno-unused-variable")

find_package(arrow REQUIRED arrow_shared)
find_package(TBB REQUIRED tbb tbbmalloc tbbmalloc_proxy)

file(READ /proc/sys/vm/nr_hugepages HPAGES)
if((NOT HPAGES GREATER 0) AND NOT BENCH_NO_HUGEPAGES)
  message( FATAL_ERROR "\
  Huge pages are not configured. Run `sudo bash -c 'echo 30000 > /proc/sys/vm/nr_hugepages'`\n\
  You can disable this warning by setting BENCH_NO_HUGEPAGES but performance will be worse and blocksize will need tuning for the (second) best performance" )
endif()

link_directories(${CONDA_PREFIX}/lib)

add_executable(bench_csv_arrow benchmark.cpp)
target_compile_features(bench_csv_arrow PRIVATE cxx_std_17)
target_compile_definitions(bench_csv_arrow PRIVATE BENCH_CSV_ONLY)
target_include_directories(bench_csv_arrow PRIVATE . ${CMAKE_INSTALL_PREFIX}/include)
target_link_libraries(bench_csv_arrow arrow_shared TBB::tbb
                      ${arrow_IMPORTED_TARGETS} ${CMAKE_DL_LIBS})  # Link "rt" library on Linux

add_executable(bench_csv_tbbmalloc benchmark.cpp)
target_compile_features(bench_csv_tbbmalloc PRIVATE cxx_std_17)
target_compile_definitions(bench_csv_tbbmalloc PRIVATE BENCH_CSV_ONLY)
target_include_directories(bench_csv_tbbmalloc PRIVATE . ${CMAKE_INSTALL_PREFIX}/include)
target_link_libraries(bench_csv_tbbmalloc arrow_shared TBB::tbb TBB::tbbmalloc_proxy
                      ${arrow_IMPORTED_TARGETS} ${CMAKE_DL_LIBS})  # Link "rt" library on Linux

add_executable(bench_csv_tbbmalloc_huge benchmark.cpp)
target_compile_features(bench_csv_tbbmalloc_huge PRIVATE cxx_std_17)
target_compile_definitions(bench_csv_tbbmalloc_huge PRIVATE BENCH_CSV_ONLY BENCH_USE_TBB_HUGE_PAGES)
target_include_directories(bench_csv_tbbmalloc_huge PRIVATE . ${CMAKE_INSTALL_PREFIX}/include)
target_link_libraries(bench_csv_tbbmalloc_huge arrow_shared ${TBB_IMPORTED_TARGETS}
                      ${arrow_IMPORTED_TARGETS} ${CMAKE_DL_LIBS})  # Link "rt" library on Linux

add_executable(bench_all_tbb benchmark.cpp)
target_compile_features(bench_all_tbb PRIVATE cxx_std_17)
target_compile_definitions(bench_all_tbb PRIVATE)  # no BENCH_USE_TBB_HUGE_PAGES
target_include_directories(bench_all_tbb PRIVATE . ${CMAKE_INSTALL_PREFIX}/include)
target_link_libraries(bench_all_tbb arrow_shared ${TBB_IMPORTED_TARGETS}
                      ${arrow_IMPORTED_TARGETS} ${CMAKE_DL_LIBS})  # Link "rt" library on Linux

add_executable(bench_all_arrow benchmark.cpp)
target_compile_features(bench_all_arrow PRIVATE cxx_std_17)
target_compile_definitions(bench_all_arrow PRIVATE BENCH_PARALLEL_WITH_ARROW)  # no BENCH_USE_TBB_HUGE_PAGES
target_include_directories(bench_all_arrow PRIVATE . ${CMAKE_INSTALL_PREFIX}/include)
target_link_libraries(bench_all_arrow arrow_shared ${TBB_IMPORTED_TARGETS}
                      ${arrow_IMPORTED_TARGETS} ${CMAKE_DL_LIBS})  # Link "rt" library on Linux

