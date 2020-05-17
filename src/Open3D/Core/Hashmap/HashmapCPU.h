// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#pragma once

// Interface for the CPU hashmap. Separated from HashmapCPU.hpp for brevity.

#include <unordered_map>
#include "Open3D/Core/Hashmap/HashmapBase.h"
#include "Open3D/Core/Hashmap/Traits.h"

namespace open3d {

template <typename Hash, typename KeyEq>
class CPUHashmap : public Hashmap<Hash, KeyEq> {
public:
    ~CPUHashmap();

    CPUHashmap(size_t init_buckets,
               size_t dsize_key,
               size_t dsize_value,
               Device device);
    void Rehash(size_t buckets);

    void Insert(void* input_keys,
                void* input_values,
                iterator_t* output_iterators,
                uint8_t* output_masks,
                size_t count);

    void Find(void* input_keys,
              iterator_t* output_iterators,
              uint8_t* output_masks,
              size_t count);

    void Erase(void* input_keys, uint8_t* output_masks, size_t count);

    size_t GetIterators(iterator_t* output_iterators);

    /// Parallel iterations
    /// Only write to corresponding entries if they are not nullptr
    /// Only access input_masks if they it is not nullptr
    void UnpackIterators(iterator_t* input_iterators,
                         uint8_t* input_masks,
                         void* output_keys,
                         void* output_values,
                         size_t count);

    /// (Optionally) In-place modify iterators returned from Find
    /// Note: key cannot be changed, otherwise the semantic is violated
    void AssignIterators(iterator_t* input_iterators,
                         uint8_t* input_masks,
                         void* input_values,
                         size_t count);

    /// Bucket-related utilities
    /// Return number of elems per bucket
    std::vector<size_t> BucketSizes();

    // /// Return size / bucket_count
    float LoadFactor() { return 0.0f; };

private:
    std::shared_ptr<std::unordered_map<uint8_t*, uint8_t*, Hash, KeyEq>>
            cpu_hashmap_impl_;

    // Valid kv_pairs
    std::vector<iterator_t> kv_pairs_;
};
}  // namespace open3d