#ifndef GROUP_BY_H
#define GROUP_BY_H

#define TBB_USE_PERFORMANCE_WARNINGS 1
#define __TBB_STATISTICS 1

#include <arrow/api.h>
#include <unordered_map>

#include <print.h>
#include <util.h>
#include <parallel.h>
#include <tbb/tbb.h>
#include <atomic>
#include <cassert>

//++++++++++++++++++++++++++++++
// GROUP BY
//++++++++++++++++++++++++++++++

// For both single and multiple columns
struct group {
    std::vector<std::vector<int>> redirection;
    std::vector<std::shared_ptr<arrow::Column>> columns;
    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::atomic<int> max_index;
    group(int n = 0): redirection(n), max_index(0) {}
    int get_max_index() { return max_index.load(std::memory_order_relaxed); }
    int increment_index() { return max_index++; }
};

// For multiple columns
struct position {
    int row_index; // TODO like in sort? without array?
    std::vector<arrow::Array*> *arrays;

    bool operator==(const position& other) const {
        for (int i = 0; i < (*arrays).size(); i++) {
            // RangeEquals doesn't work
            //if ((*arrays)[i]->RangeEquals(row_index, row_index+1, other.row_index, (*other.arrays)[i]) == false) {
            if (compare((*arrays)[i], row_index, (*other.arrays)[i], other.row_index) != 0) {
                return false;
            }
        }
        return true;
    }
    operator size_t() const {
        // Compute individual hash values for two data members and combine them using XOR and bit shifting
        size_t answer = 0;
        for (int i = 0; i < arrays->size(); i++) { // TODO predefine type somehow
            arrow::Array *row = (*arrays)[i];
            if (row->type_id() == arrow::Type::STRING) {
                auto array = (arrow::StringArray*)row;
                answer ^= std::hash<std::string>()(array->GetString(row_index));
            } else if (row->type_id() == arrow::Type::DOUBLE) {
                auto array = (arrow::DoubleArray*)row;
                answer ^= std::hash<double>()(array->Value(row_index));
                //TODO answer ^= ((hash<float>()(k.getM()) ^ (hash<float>()(k.getC()) << 1)) >> 1);
            } else {
                auto array = (arrow::Int64Array*)row;
                answer ^= std::hash<int64_t>()(array->Value(row_index));
                //TODO answer ^= ((hash<float>()(k.getM()) ^ (hash<float>()(k.getC()) << 1)) >> 1);
            }
        }
        return answer;
    }
};

#if 1 //USE_TBB
template<typename K>
struct group_map : public tbb::concurrent_hash_map<K, int> {
  using base_t=tbb::concurrent_hash_map<K, int>;
  using base_t::concurrent_hash_map;
#if TBB_INTERFACE_VERSION < 11007
  typename base_t::const_pointer fast_find(const typename base_t::key_type& k) {
    return this->internal_fast_find(k);
  }
#elif TBB_INTERFACE_VERSION < 11009
  typename base_t::const_pointer fast_find( const typename base_t::key_type& key ) const {
      typedef typename base_t::hashcode_t hashcode_t;
      hashcode_t h = this->my_hash_compare.hash( key );
      hashcode_t m = (hashcode_t) itt_load_word_with_acquire( this->my_mask );
      typename base_t::node *n;
  restart:
      __TBB_ASSERT((m&(m+1))==0, "data structure is invalid");
      auto *b = this->get_bucket( h & m );
      // TODO: actually, notification is unnecessary here, just hiding double-check
      if( itt_load_word_with_acquire(b->node_list) == tbb::interface5::internal::rehash_req )
          return 0; // expecting group_by to double check with locked insert
      n = this->search_bucket( key, b );
      if( n )
          return n->storage();
      else if( this->check_mask_race( h, m ) )
          goto restart;
      return 0;
  }
#endif
};

typedef group_map<position> mult_group_map_t;

void group_by_sequential_multiple( std::vector<arrow::Array*> *arrays
                                 , std::vector<int> &redir, group *g, mult_group_map_t* pg) {
    position p{0, arrays};
    for (int i = 0, end_i = (*arrays)[0]->length(); i < end_i; i++) {
        p.row_index = i;
        auto *x = pg->fast_find(p);
        if(x && x->second >= 0)
            redir[i] = x->second;
        else {
            mult_group_map_t::accessor a;
            bool uniq = pg->insert(a, {p, -1});
            if (!uniq) {
                redir[i] = a->second;
            } else {
                a->second = redir[i] = g->increment_index();
            }
        }
    }
}

#else

namespace std {
template <>
// TODO user defined hash function
struct hash<position> {
    size_t operator()(const position& p) const { // TODO type?
        return size_t(p);
    }
};
}
typedef std::unordered_map<position, int> mult_group_map_t;

void group_by_sequential_multiple( std::vector<std::shared_ptr<arrow::Array>> *arrays
                                 , std::vector<int> &redir, group *g, mult_group_map_t* pg, int n) {
    for (int i = 0, end_i = (*arrays)[0]->length(); i < end_i; i++) {
        position p{i, arrays}; // TODO copy constructor? move constructor?
        auto number = pg->find(p);
        if (number != pg->end()) {
            redir[i] = number->second;
        } else {
            int new_index = g->increment_index();
            redir[i] = new_index;
            pg->insert({p, new_index});
        }
    }
}
#endif

group* group_by_parallel_multiple(std::shared_ptr<arrow::Table> table, std::vector<int> column_ids) {
    printf("      Arrow is columnar database and this request is low performance\n");
    printf("      There are two variants: prebuild hashes or not. Executing _without_ prebuilding\n");
    // Can different columns have different chunk number? Or it is property of table?
    auto *column0 = table->column(column_ids[0])->data().get();
    int num_chunks = column0->num_chunks();

    auto *g = new group{num_chunks};
    std::vector<std::vector<arrow::Array*>> all_arrays(num_chunks);
    mult_group_map_t pg(2048);

    for(int i = 0; i < num_chunks; i++) {
        auto &chunk = all_arrays[i];
        for (int j = 0; j < column_ids.size(); j++) {
            chunk.push_back(table->column(column_ids[j])->data()->chunk(i).get());
        }
    }
#if 1 //USE_TBB
    parallel_for(num_chunks, [&,g,column0](int i) {
    //for(int i = 0; i < num_chunks; i++) {
    //for(int i = num_chunks-1; i >= 0; i--) {
        auto &chunk = all_arrays[i];
        auto &redir = g->redirection[i];
        redir.resize(column0->chunk(i)->length());
        group_by_sequential_multiple(&chunk, redir, g, &pg);
    });
#endif
    for (int i = 0; i < column_ids.size(); i++) {
      std::shared_ptr<arrow::ChunkedArray> ca = table->column(column_ids[i])->data();
      std::shared_ptr<arrow::Array> data;
      if (ca->type()->id() == arrow::Type::STRING) {
        std::vector<std::string> new_column(pg.size());
        for (auto j = pg.begin(); j != pg.end(); j++) {
          new_column[j->second] = ((arrow::StringArray*)((*j->first.arrays)[i]))->GetString(j->first.row_index);
        }
        data = vector_to_array<std::string, arrow::StringBuilder>(new_column);
      } else if (ca->type()->id() == arrow::Type::INT64) {
        std::vector<int64_t> new_column(pg.size());
        for (auto j = pg.begin(); j != pg.end(); j++) {
          new_column[j->second] = ((arrow::Int64Array*)((*j->first.arrays)[i]))->Value(j->first.row_index);
        }
        data = vector_to_array<arrow::Int64Type::c_type, arrow::Int64Builder>(new_column);
      } else {
        std::vector<double> new_column(pg.size());
        for (auto j = pg.begin(); j != pg.end(); j++) {
          new_column[j->second] = ((arrow::DoubleArray*)((*j->first.arrays)[i]))->Value(j->first.row_index);
        }
        data = vector_to_array<arrow::DoubleType::c_type, arrow::DoubleBuilder>(new_column);
      }
      std::shared_ptr<arrow::Field> field = table->schema()->field(column_ids[i]);
      g->columns.push_back(std::make_shared<arrow::Column>(field->name(), data));
      g->fields.push_back(field);
    }
    return g;
}

// For single column

#if 1 //USE_TBB
template <typename T, typename T2>
void group_by_sequential_single( T2 *array, std::vector<int> &redir, group *g, group_map<T>* pg) {
    for (int i = 0, end_i = array->length(); i < end_i; i++) {
        T key = get_value<T, T2>(array, i);
        auto *x = pg->fast_find(key);
        if(x && x->second >= 0)
            redir[i] = x->second;
        else {
            typename group_map<T>::accessor a;
            bool uniq = pg->insert(a, typename group_map<T>::value_type{key, -1});
            if (!uniq) {
                redir[i] = a->second;
            } else {
                auto idx = g->increment_index();
                a->second = idx;
                redir[i] = idx;
            }
        }
    }
}

#else

template <typename T, typename T2>
void group_by_sequential_single(T2* array, group *g, group_map<T>* pg) {
    int s = g->redirection.size();
    for (int i = 0; i < array->length(); i++) {
        T value = get_value<T, T2>(array, i);
        auto number = pg->find(value);
        if (number != pg->end()) {
            g->redirection[s].push_back(number->second);
        } else {
            g->redirection[s].push_back(pg->size());
            pg->insert({value, pg->size()});
        }
    }
}
#endif

template <typename T, typename T2, typename T4>
group* group_by_parallel_single(std::shared_ptr<arrow::Column> column) {
    group_map<T> pg(2048);//67108864);
    auto *ca = column->data().get();
    int num_chunks = ca->num_chunks();
    auto *g = new group(num_chunks);

    //for (int i = 0; i < num_chunks; i++) {
    parallel_for(num_chunks, [&,ca,g](int i) {
        T2 *array = (T2*)ca->chunk(i).get();
        auto &redir = g->redirection[i];
        redir.resize(array->length());
        group_by_sequential_single<T, T2>(array, redir, g, &pg);
    } );
    assert(g->max_index == pg.size());

    std::shared_ptr<arrow::Array> data;
    std::vector<T> new_column(pg.size());
    for (auto j = pg.begin(); j != pg.end(); j++) {
        new_column[j->second] = j->first;
    }
    data = vector_to_array<T, T4>(new_column);
    std::shared_ptr<arrow::Field> field = column->field();
    g->columns.push_back(std::make_shared<arrow::Column>(field->name(), data));
    g->fields.push_back(field);

    return g;
}

group* group_by_dispatch(std::shared_ptr<arrow::Table> table, int column_id) {
    std::shared_ptr<arrow::Column> column = table->column(column_id);
    if (column->type()->id() == arrow::Type::STRING) {
        return group_by_parallel_single<std::string, arrow::StringArray, arrow::StringBuilder>(column);
    } else if (column->type()->id() == arrow::Type::INT64) {
        return group_by_parallel_single<arrow::Int64Type::c_type, arrow::Int64Array, arrow::Int64Builder>(column);
    } else {
        return group_by_parallel_single<arrow::DoubleType::c_type, arrow::DoubleArray, arrow::DoubleBuilder>(column);
    }
}

// Main function
group* group_by(std::shared_ptr<arrow::Table> table, std::vector<int> column_ids) {
    printf("TASK: grouping by %s. (building all group_by columns and NOT counting them)\n", column_ids.size() == 1 ? "single column" : "multiple columns");
    auto begin = std::chrono::steady_clock::now();
    group *g;
    if (column_ids.size() == 1) {
        g = group_by_dispatch(table, column_ids[0]);
    } else  {
        g = group_by_parallel_multiple(table, column_ids);
    }
    print_time(begin);
    return g;
}

#endif
