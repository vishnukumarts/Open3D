#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>
#include "Hashmap.h"

template <typename T, size_t D>
struct Coordinate {
private:
    T data_[D];

public:
    __device__ __host__ T& operator[](size_t i) { return data_[i]; }
    __device__ __host__ const T& operator[](size_t i) const { return data_[i]; }

    __device__ __host__ bool operator==(const Coordinate<T, D>& rhs) const {
        bool equal = true;
#pragma unroll 1
        for (size_t i = 0; i < D; ++i) {
            equal = equal && (data_[i] == rhs[i]);
        }
        return equal;
    }

    static __host__ Coordinate<T, D> random(
            std::default_random_engine generator,
            std::uniform_int_distribution<int> dist) {
        Coordinate<T, D> res;
        for (size_t i = 0; i < D; ++i) {
            res.data_[i] = dist(generator);
        }
        return res;
    }
};

template <typename T, size_t D>
struct CoordinateHashFunc {
    __device__ __host__ uint64_t operator()(const Coordinate<T, D>& key) const {
        uint64_t hash = UINT64_C(14695981039346656037);

        /** We only support 4-byte and 8-byte types **/
        using input_t = typename std::conditional<sizeof(T) == sizeof(uint32_t),
                                                  uint32_t, uint64_t>::type;
#pragma unroll 1
        for (size_t i = 0; i < D; ++i) {
            hash ^= *((input_t*)(&key[i]));
            hash *= UINT64_C(1099511628211);
        }
        return hash;
    }
};

struct Vector6i {
    int x[6];

    __device__ __host__ Vector6i(){};
    __host__ Vector6i Random_(std::default_random_engine& generator,
                              std::uniform_int_distribution<int>& dist) {
        for (int i = 0; i < 6; ++i) {
            x[i] = dist(generator);
        }
        return *this;
    }

    __device__ __host__ bool operator==(const Vector6i& other) const {
        bool res = true;
        for (int i = 0; i < 6; ++i) {
            res = res && (other.x[i] == x[i]);
        }
        return res;
    }
};

namespace std {
template <>
struct hash<Vector6i> {
    std::size_t operator()(const Vector6i& k) const {
        uint64_t h = UINT64_C(14695981039346656037);
        for (size_t i = 0; i < 6; ++i) {
            h ^= k.x[i];
            h *= UINT64_C(1099511628211);
        }
        return h;
    }
};
}

void TEST_SIMPLE() {
    std::unordered_map<int, int> unordered_map;

    // insert
    std::vector<int> insert_keys = {1, 3, 5};
    std::vector<int> insert_vals = {100, 300, 500};
    for (int i = 0; i < insert_keys.size(); ++i) {
        unordered_map[insert_keys[i]] = insert_vals[i];
    }

    cuda::Hashmap<int, int> cuda_unordered_map(10);
    thrust::device_vector<int> cuda_insert_keys = insert_keys;
    thrust::device_vector<int> cuda_insert_vals = insert_vals;
    cuda_unordered_map.Insert(cuda_insert_keys, cuda_insert_vals);

    // query
    thrust::device_vector<int> cuda_query_keys(
            std::vector<int>({1, 2, 3, 4, 5}));
    auto cuda_query_results = cuda_unordered_map.Search(cuda_query_keys);

    for (int i = 0; i < cuda_query_keys.size(); ++i) {
        auto iter = unordered_map.find(cuda_query_keys[i]);
        if (iter == unordered_map.end()) {
            assert(cuda_query_results.second[i] == 0);
        } else {
            Iterator<int, int> iterator = cuda_query_results.first[i];
            Pair<int, int> kv =
                    *(thrust::device_ptr<Pair<int, int>>(iterator));
            assert(kv.first == cuda_query_keys[i]);
            assert(kv.second == iter->second);
        }
    }

    std::cout << "TEST_SIMPLE() passed\n";
}

void TEST_6DIM_KEYS(int key_size) {
    std::default_random_engine generator;
    std::uniform_int_distribution<int> dist(-1000, 1000);

    // generate data
    std::cout << "generating data...\n";
    std::vector<Vector6i> insert_keys(key_size);
    std::vector<int> insert_vals(key_size);
    for (int i = 0; i < key_size; ++i) {
        insert_keys[i].Random_(generator, dist);
        insert_vals[i] = i;
    }
    thrust::device_vector<Vector6i> cuda_insert_keys = insert_keys;
    thrust::device_vector<int> cuda_insert_vals = insert_vals;
    std::cout << "data generated\n";

    // cpu groundtruth
    std::cout << "generating std::unordered_map ground truth...\n";
    std::unordered_map<Vector6i, int> unordered_map;
    for (int i = 0; i < key_size; ++i) {
        unordered_map[insert_keys[i]] = insert_vals[i];
    }
    std::cout << "ground truth generated\n";

    // gpu test
    std::cout << "inserting to cuda::Hashmap...\n";
    cuda::Hashmap<Vector6i, int> cuda_unordered_map(key_size);
    cuda_unordered_map.Insert(cuda_insert_keys, cuda_insert_vals);
    std::cout << "insertion finished\n";

    // query -- all true
    std::cout << "generating query_data...\n";
    thrust::device_vector<Vector6i> cuda_query_keys(insert_keys.size());
    for (int i = 0; i < key_size; ++i) {
        if (i % 3 == 2) {
            cuda_query_keys[i] = cuda_insert_keys[i];
        } else {
            cuda_query_keys[i] = Vector6i().Random_(generator, dist);
        }
    }
    std::cout << "query data generated\n";

    std::cout << "query from cuda::Hashmap...\n";
    auto cuda_query_results = cuda_unordered_map.Search(cuda_query_keys);
    std::cout << "query results generated\n";

    std::cout << "comparing query results against ground truth...\n";
    for (int i = 0; i < cuda_query_keys.size(); ++i) {
        auto iter = unordered_map.find(cuda_query_keys[i]);
        if (iter == unordered_map.end()) {
            assert(cuda_query_results.second[i] == 0);
        } else {
            Iterator<Vector6i, int> iterator = cuda_query_results.first[i];
            // Iterator == Pair*
            Pair<Vector6i, int> kv =
                    *(thrust::device_ptr<Pair<Vector6i, int>>(iterator));
            assert(kv.first == cuda_query_keys[i]);
            assert(kv.second == iter->second);
        }
    }

    std::cout << "TEST_6DIM_KEYS() passed\n";
}


void TEST_COORD_KEYS(int key_size) {
    const int D = 8;
    std::default_random_engine generator;
    std::uniform_int_distribution<int> dist(-1000, 1000);

    // generate raw data (a bunch of data mimicking at::Tensor)
    std::cout << "generating data...\n";
    std::vector<int> input_coords(key_size * D);
    for (int i = 0; i < key_size * D; ++i) {
        input_coords[i] = dist(generator);
    }
    std::cout << "data generated\n";

    // convert raw data (at::Tensor) to std::vector
    // and prepare indices
    std::cout << "converting format...\n";
    std::vector<Coordinate<int, D>> insert_keys(key_size);
    std::memcpy(insert_keys.data(), input_coords.data(),
                sizeof(int) * key_size * D);
    std::vector<int> insert_vals(key_size);
    std::iota(insert_vals.begin(), insert_vals.end(), 0);

    // also make sure memcpy works correctly
    for (int i = 0; i < key_size; ++i) {
        for (int d = 0; d < D; ++d) {
            assert(input_coords[i * D + d] == insert_keys[i][d]);
        }
    }
    std::cout << "conversion finished\n";

    // cpu groundtruth
    std::cout << "generating std::unordered_map ground truth hashtable...\n";
    std::unordered_map<Coordinate<int, D>, int, CoordinateHashFunc<int, D>>
            unordered_map;
    for (int i = 0; i < key_size; ++i) {
        unordered_map[insert_keys[i]] = insert_vals[i];
    }
    std::cout << "ground truth generated\n";

    // gpu test
    std::cout << "inserting to cuda::Hashmap...\n";
    thrust::device_vector<Coordinate<int, D>> cuda_insert_keys = insert_keys;
    thrust::device_vector<int> cuda_insert_vals = insert_vals;
    cuda::Hashmap<Coordinate<int, D>, int> cuda_unordered_map(key_size);
    cuda_unordered_map.Insert(cuda_insert_keys, cuda_insert_vals);
    std::cout << "insertion finished\n";

    // query
    std::cout << "generating query_data...\n";
    thrust::device_vector<Coordinate<int, D>> cuda_query_keys(insert_keys.size());
    for (int i = 0; i < key_size; ++i) {
        if (i % 3 != 2) {  // 2/3 is valid
            cuda_query_keys[i] = cuda_insert_keys[i];
        } else {  // 1/3 is invalid
            cuda_query_keys[i] = Coordinate<int, D>::random(generator, dist);
        }
    }
    std::cout << "query data generated\n";

    std::cout << "query from cuda::Hashmap...\n";
    auto cuda_query_results = cuda_unordered_map.Search(cuda_query_keys);
    std::cout << "query results generated\n";

    std::cout << "comparing query results against ground truth...\n";
    for (int i = 0; i < cuda_query_keys.size(); ++i) {
        auto iter = unordered_map.find(cuda_query_keys[i]);
        if (iter == unordered_map.end()) {
            assert(cuda_query_results.second[i] == 0);
        } else {
            Iterator<Coordinate<int, D>, int> iterator =
                    cuda_query_results.first[i];
            // Iterator == Pair*
            Pair<Coordinate<int, D>, int> kv =
                    *(thrust::device_ptr<Pair<Coordinate<int, D>, int>>(
                            iterator));
            assert(kv.first == cuda_query_keys[i]);
            assert(kv.second == iter->second);
        }
    }

    std::cout << "TEST_COORD_KEYS() passed\n";
}

int main() {
    TEST_SIMPLE();
    TEST_6DIM_KEYS(1000000);
    TEST_COORD_KEYS(1000000);
}