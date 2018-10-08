//
// Created by wei on 10/1/18.
//

#include "RGBDOdometryCuda.cuh"
#include "Reduction2DCuda.cuh"

namespace three {

template<size_t N>
__global__
void ApplyRGBDOdometryKernel(RGBDOdometryCudaServer<N> odometry, size_t level) {
	/** Add more memory blocks if we have **/
	/** TODO: check this version vs 1 __shared__ array version **/
	static __shared__ float local_sum0[THREAD_2D_UNIT * THREAD_2D_UNIT];
	static __shared__ float local_sum1[THREAD_2D_UNIT * THREAD_2D_UNIT];
	static __shared__ float local_sum2[THREAD_2D_UNIT * THREAD_2D_UNIT];

	const int x = threadIdx.x + blockIdx.x * blockDim.x;
	const int y = threadIdx.y + blockIdx.y * blockDim.y;
	const int tid = threadIdx.x + threadIdx.y * blockDim.x;

	/** Proper initialization **/
	local_sum0[tid] = 0;
	local_sum1[tid] = 0;
	local_sum2[tid] = 0;

	if (x >= odometry.source_depth().level(level).width_
		|| y >= odometry.source_depth().level(level).height_)
		return;

	/** Compute Jacobian and residual **/
	JacobianCuda<6> jacobian_I, jacobian_D;
	float residual_I, residual_D;
	bool mask = odometry.ComputePixelwiseJacobiansAndResiduals(
		x, y, level, jacobian_I, jacobian_D, residual_I, residual_D);
	if (!mask) return;

	/** Compute JtJ and Jtr **/
	HessianCuda<6> JtJ;
	Vector6f Jtr;
	odometry.ComputePixelwiseJtJAndJtr(jacobian_I, jacobian_D,
									   residual_I, residual_D,
									   JtJ, Jtr);
	odometry.target_on_source().level(level)(x, y) = Vector1f(0.0f);

	/** Reduce Sum JtJ **/
#pragma unroll 1
	for (size_t i = 0; i < 21; i += 3) {
		local_sum0[tid] = JtJ(i + 0);
		local_sum1[tid] = JtJ(i + 1);
		local_sum2[tid] = JtJ(i + 2);
		__syncthreads();

		if (tid < 128) {
			local_sum0[tid] += local_sum0[tid + 128];
			local_sum1[tid] += local_sum1[tid + 128];
			local_sum2[tid] += local_sum2[tid + 128];
		}
		__syncthreads();

		if (tid < 64) {
			local_sum0[tid] += local_sum0[tid + 64];
			local_sum1[tid] += local_sum1[tid + 64];
			local_sum2[tid] += local_sum2[tid + 64];
		}
		__syncthreads();

		if (tid < 32) {
			WarpReduceSum<float>(local_sum0, tid);
			WarpReduceSum<float>(local_sum1, tid);
			WarpReduceSum<float>(local_sum2, tid);
		}

		if (tid == 0) {
			atomicAdd(&odometry.results().get(i + 0), local_sum0[0]);
			atomicAdd(&odometry.results().get(i + 1), local_sum1[0]);
			atomicAdd(&odometry.results().get(i + 2), local_sum2[0]);
		}
		__syncthreads();
	}

	/** Reduce Sum Jtr **/
#define OFFSET1 21
#pragma unroll 1
	for (size_t i = 0; i < 6; i += 3) {
		local_sum0[tid] = Jtr(i + 0);
		local_sum1[tid] = Jtr(i + 1);
		local_sum2[tid] = Jtr(i + 2);
		__syncthreads();

		if (tid < 128) {
			local_sum0[tid] += local_sum0[tid + 128];
			local_sum1[tid] += local_sum1[tid + 128];
			local_sum2[tid] += local_sum2[tid + 128];
		}
		__syncthreads();

		if (tid < 64) {
			local_sum0[tid] += local_sum0[tid + 64];
			local_sum1[tid] += local_sum1[tid + 64];
			local_sum2[tid] += local_sum2[tid + 64];
		}
		__syncthreads();

		if (tid < 32) {
			WarpReduceSum<float>(local_sum0, tid);
			WarpReduceSum<float>(local_sum1, tid);
			WarpReduceSum<float>(local_sum2, tid);
		}

		if (tid == 0) {
			atomicAdd(&odometry.results().get(i + 0 + OFFSET1), local_sum0[0]);
			atomicAdd(&odometry.results().get(i + 1 + OFFSET1), local_sum1[0]);
			atomicAdd(&odometry.results().get(i + 2 + OFFSET1), local_sum2[0]);
		}
		__syncthreads();
	}

	/** Reduce Sum residual and inlier **/
#define OFFSET2 27
	{

		local_sum0[tid] = residual_I * residual_I + residual_D * residual_D;
		local_sum1[tid] = 1;
		__syncthreads();

		if (tid < 128) {
			local_sum0[tid] += local_sum0[tid + 128];
			local_sum1[tid] += local_sum1[tid + 128];
		}
		__syncthreads();

		if (tid < 64) {
			local_sum0[tid] += local_sum0[tid + 64];
			local_sum1[tid] += local_sum1[tid + 64];
		}
		__syncthreads();

		if (tid < 32) {
			WarpReduceSum<float>(local_sum0, tid);
			WarpReduceSum<float>(local_sum1, tid);
		}

		if (tid == 0) {
			atomicAdd(&odometry.results().get(OFFSET2 + 0), local_sum0[0]);
			atomicAdd(&odometry.results().get(OFFSET2 + 1), local_sum1[0]);
		}
		__syncthreads();
	}
}

}