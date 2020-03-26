/**
 * Created by wei on 18-3-29.
 */

#pragma once

#include <assert.h>
#include <memory>
#include <vector>
#include "Open3D/Core/CUDAUtils.h"
#include "Open3D/Core/MemoryManager.h"

#include "Consts.h"

/// Dynamic memory allocation and free are expensive on kernels.
/// We pre-allocate a chunk of memory and manually manage them on kernels.
/// For simplicity, we maintain a chunk per array (type T) instead of managing a
/// universal one. This causes more redundancy but is easier to maintain.
template <typename T>
class InternalMemoryManagerContext {
public:
    T *data_;           /* [N] */
    ptr_t *heap_;       /* [N] */
    int *heap_counter_; /* [1] */

public:
    int max_capacity_;

public:
    /**
     * The @value array's size is FIXED.
     * The @heap array stores the addresses of the values.
     * Only the unallocated part is maintained.
     * (ONLY care about the heap above the heap counter. Below is meaningless.)
     * ---------------------------------------------------------------------
     * heap  ---Malloc-->  heap  ---Malloc-->  heap  ---Free(0)-->  heap
     * N-1                 N-1                  N-1                  N-1   |
     *  .                   .                    .                    .    |
     *  .                   .                    .                    .    |
     *  .                   .                    .                    .    |
     *  3                   3                    3                    3    |
     *  2                   2                    2 <-                 2    |
     *  1                   1 <-                 1                    0 <- |
     *  0 <- heap_counter   0                    0                    0
     */
    __device__ ptr_t Allocate() {
        int index = atomicAdd(heap_counter_, 1);
#ifdef CUDA_DEBUG_ENABLE_ASSERTION
        assert(index < max_capacity_);
#endif
        return heap_[index];
    }

    __device__ void Free(ptr_t ptr) {
        int index = atomicSub(heap_counter_, 1);
#ifdef CUDA_DEBUG_ENABLE_ASSERTION
        assert(index >= 1);
#endif
        heap_[index - 1] = ptr;
    }

    __device__ T &extract(ptr_t ptr) {
#ifdef CUDA_DEBUG_ENABLE_ASSERTION
        assert(addr < max_capacity_);
#endif
        return data_[ptr];
    }

    __device__ const T &extract(ptr_t ptr) const {
#ifdef CUDA_DEBUG_ENABLE_ASSERTION
        assert(addr < max_capacity_);
#endif
        return data_[ptr];
    }

    /* Returns the real ptr that can be accessed (instead of the internal ptr)
     */
    __device__ T *extract_ext_ptr(ptr_t ptr) { return data_ + ptr; }
};

template <typename T>
__global__ void ResetInternalMemoryManagerKernel(
        InternalMemoryManagerContext<T> ctx) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < ctx.max_capacity_) {
        ctx.data_[i] = T(); /* This is not required. */
        ctx.heap_[i] = i;
    }
}

template <typename T, class Alloc>
class InternalMemoryManager {
public:
    int max_capacity_;
    InternalMemoryManagerContext<T> gpu_context_;
    open3d::Device device_;

public:
    InternalMemoryManager(int max_capacity, open3d::Device device) {
        device_ = device;
        max_capacity_ = max_capacity;
        gpu_context_.max_capacity_ = max_capacity;

        gpu_context_.heap_counter_ = static_cast<int *>(
                Alloc::Malloc(size_t(1) * sizeof(int), device_));
        gpu_context_.heap_ = static_cast<ptr_t *>(
                Alloc::Malloc(size_t(max_capacity_) * sizeof(ptr_t), device_));
        gpu_context_.data_ = static_cast<T *>(
                Alloc::Malloc(size_t(max_capacity_) * sizeof(T), device_));

        const int blocks = (max_capacity_ + 128 - 1) / 128;
        const int threads = 128;

        ResetInternalMemoryManagerKernel<<<blocks, threads>>>(gpu_context_);
        OPEN3D_CUDA_CHECK(cudaDeviceSynchronize());
        OPEN3D_CUDA_CHECK(cudaGetLastError());

        int heap_counter = 0;
        Alloc::Memcpy(gpu_context_.heap_counter_, device_, &heap_counter,
                      open3d::Device("CPU:0"), sizeof(int));
    }

    ~InternalMemoryManager() {
        Alloc::Free(gpu_context_.heap_counter_, device_);
        Alloc::Free(gpu_context_.heap_, device_);
        Alloc::Free(gpu_context_.data_, device_);
    }

    std::vector<int> DownloadHeap() {
        std::vector<int> ret;
        ret.resize(max_capacity_);
        Alloc::Memcpy(ret.data(), open3d::Device("CPU:0"), gpu_context_.heap_,
                      device_, sizeof(int) * max_capacity_);
        return ret;
    }

    std::vector<T> DownloadValue() {
        std::vector<T> ret;
        ret.resize(max_capacity_);
        Alloc::Memcpy(ret.data(), open3d::Device("CPU:0"), gpu_context_.data_,
                      device_, sizeof(T) * max_capacity_);
        return ret;
    }

    int heap_counter() {
        int heap_counter;
        Alloc::Memcpy(&heap_counter, open3d::Device("CPU:0"),
                      gpu_context_.heap_counter_, device_, sizeof(int));
        return heap_counter;
    }
};