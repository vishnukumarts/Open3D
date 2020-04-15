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

#include "Open3D/Core/CUDAUtils.h"
#include "Open3D/Core/Hashmap/Types.h"
#include "Open3D/Core/MemoryManager.h"

namespace open3d {

class Hashmap {
public:
    using MemMgr = open3d::MemoryManager;

    Hashmap(uint32_t max_keys,
            uint32_t dsize_key,
            uint32_t dsize_value,
            open3d::Device device,
            hash_t hash_fn_ptr)
        : max_keys_(max_keys),
          dsize_key_(dsize_key),
          dsize_value_(dsize_value),
          device_(device){};

    virtual std::pair<iterator_t*, uint8_t*> Insert(
            uint8_t* input_keys,
            uint8_t* input_values,
            uint32_t input_key_size) = 0;

    virtual std::pair<iterator_t*, uint8_t*> Search(
            uint8_t* input_keys, uint32_t input_key_size) = 0;

    virtual uint8_t* Remove(uint8_t* input_keys, uint32_t input_key_size) = 0;

protected:
    uint32_t max_keys_;
    uint32_t dsize_key_;
    uint32_t dsize_value_;

    open3d::Device device_;
};

class CUDAHashmapImpl;

template <typename Hash>
class CUDAHashmap : public Hashmap {
public:
    ~CUDAHashmap();

    CUDAHashmap(uint32_t max_keys,
                uint32_t dsize_key,
                uint32_t dsize_value,
                open3d::Device device,
                hash_t hash_fn_ptr);

    std::pair<iterator_t*, uint8_t*> Insert(uint8_t* input_keys,
                                            uint8_t* input_values,
                                            uint32_t input_key_size);

    std::pair<iterator_t*, uint8_t*> Search(uint8_t* input_keys,
                                            uint32_t input_key_size);

    uint8_t* Remove(uint8_t* input_keys, uint32_t input_key_size);

protected:
    uint32_t num_buckets_;

    // Buffer to store temporary results
    uint8_t* output_key_buffer_;
    uint8_t* output_value_buffer_;
    iterator_t* output_iterator_buffer_;
    uint8_t* output_mask_buffer_;

    std::shared_ptr<CUDAHashmapImpl> cuda_hashmap_impl_;
};

class CPUHashmap : public Hashmap {
public:
    ~CPUHashmap();

    CPUHashmap(uint32_t max_keys,
               uint32_t dsize_key,
               uint32_t dsize_value,
               open3d::Device device,
               hash_t hash_fn_ptr);

    std::pair<iterator_t*, uint8_t*> Insert(uint8_t* input_keys,
                                            uint8_t* input_values,
                                            uint32_t input_key_size);

    std::pair<iterator_t*, uint8_t*> Search(uint8_t* input_keys,
                                            uint32_t input_key_size);

    uint8_t* Remove(uint8_t* input_keys, uint32_t input_key_size);
};

/// Factory
std::shared_ptr<Hashmap> CreateHashmap(uint32_t max_keys,
                                       uint32_t dsize_key,
                                       uint32_t dsize_value,
                                       open3d::Device device,
                                       hash_t hash_fn_ptr);
}  // namespace open3d
