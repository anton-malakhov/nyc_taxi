#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <arrow/api.h>
#include <parallel.h>
#include <print.h>

//++++++++++++++++++++++++++++++
// TRANSFORM
//++++++++++++++++++++++++++++++

// One column tranformation per time. Should aggregation be the same? No vector of tasks?
template <typename T_c, typename T_n, typename T2, typename T4>
std::shared_ptr<arrow::Table> transform(std::shared_ptr<arrow::Table> table, int column_id, T_n (*transformation)(T_c)) {
    printf("TASK: transforming of column\n");
    auto begin = std::chrono::steady_clock::now();
    const auto column = table->column(column_id)->data();
    arrow::ArrayVector new_chunks(column->num_chunks());
    printf("Parallel: %d, chunk[0]->length: %d\n", column->num_chunks(), column->chunk(0)->length());
    parallel_for(column->num_chunks(), [&new_chunks, &column, column_id, transformation](int i){
    //for(int i = column->num_chunks()-1; i >= 0; i--){
        auto c = std::dynamic_pointer_cast<T2>(column->chunk(i)); // template
        if (c == NULL) {
            printf("Type of %d column is wrong!\n", column_id + 1); abort();
        }
        T4 bld; // template
        bld.Resize(c->length());
        for (int j = 0; j < c->length(); j++) {
            auto value = c->Value(j);
            auto new_value = transformation(value);
            bld.Append(new_value); // resize?
        }
        std::shared_ptr<arrow::Array> data;
        bld.Finish(&data);
        new_chunks[i] = data;
    });

    std::shared_ptr<arrow::Field> new_field = std::make_shared<arrow::Field>("name", new_chunks[0]->type());
    std::shared_ptr<arrow::Column> new_column = std::make_shared<arrow::Column>(new_field, new_chunks);
    std::shared_ptr<arrow::Table> new_table;
    table->SetColumn(column_id, new_column, &new_table);

    print_time(begin);
    return new_table;
}

#endif
