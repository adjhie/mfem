// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#include "../../general/okina.hpp"
#include "vector.hpp"

// *****************************************************************************
MFEM_NAMESPACE

// *****************************************************************************
#ifdef __NVCC__

// *****************************************************************************
#define CUDA_BLOCKSIZE 256

// *****************************************************************************
__global__ void cuKernelDot(const size_t N, double *gdsr,
                            const double *x, const double *y){
   __shared__ double s_dot[CUDA_BLOCKSIZE];
   const size_t n = blockDim.x*blockIdx.x + threadIdx.x;
   if (n>=N) return;   
   const size_t bid = blockIdx.x;
   const size_t tid = threadIdx.x;
   const size_t bbd = bid*blockDim.x;
   const size_t rid = bbd+tid;
   s_dot[tid] = x[n] * y[n];
   __syncthreads();
   for(size_t workers=blockDim.x>>1; workers>0; workers>>=1){
      if (tid >= workers) continue;
      if (rid >= N) continue;
      const size_t dualTid = tid + workers;
      if (dualTid >= N) continue;
      const size_t rdd = bbd+dualTid;
      if (rdd >= N) continue;
      if (dualTid >= blockDim.x) continue;
      s_dot[tid] += s_dot[dualTid];
      __syncthreads();
   }
   if (tid==0) gdsr[bid] = s_dot[0];
   __syncthreads();
}

// *****************************************************************************
double cuVectorDot(const size_t N, const double *x, const double *y){
   const size_t tpb = CUDA_BLOCKSIZE;
   const size_t blockSize = CUDA_BLOCKSIZE;
   const size_t gridSize = (N+blockSize-1)/blockSize;
   const size_t dot_sz = (N%tpb)==0? (N/tpb) : (1+N/tpb);
   const size_t bytes = dot_sz*sizeof(double);   
   static double *h_dot = NULL;
   if (!h_dot) { h_dot = (double*)calloc(dot_sz,sizeof(double)); }
   static CUdeviceptr gdsr = (CUdeviceptr) NULL;
   if (!gdsr) { checkCudaErrors(cuMemAlloc(&gdsr,bytes)); }
   cuKernelDot<<<gridSize,blockSize>>>(N, (double*)gdsr, x, y);
   checkCudaErrors(cuMemcpy((CUdeviceptr)h_dot,(CUdeviceptr)gdsr,bytes));
   double dot = 0.0;
   for(size_t i=0; i<dot_sz; i+=1) dot += h_dot[i];
   return dot;
}
#endif // __NVCC__

// *****************************************************************************
double kVectorDot(const size_t N, const double *x, const double *y)
{
   GET_CUDA;
   GET_CONST_ADRS(x);
   GET_CONST_ADRS(y);
#ifdef __NVCC__
   if (cuda) { return cuVectorDot(N, d_x, d_y); }
#endif // __NVCC__
   double dot = 0.0;
   for (size_t i=0; i<N; i+=1) { dot += d_x[i] * d_y[i]; }
   return dot;
}

// *****************************************************************************
void kVectorMapDof(const int N, double *v0, const double *v1, const int *dof)
{
   GET_ADRS(v0);
   GET_CONST_ADRS(v1);
   GET_CONST_ADRS_T(dof,int);
   forall(i, N,
   {
      const int dof_i = d_dof[i];
      d_v0[dof_i] = d_v1[dof_i];
   });
}

// *****************************************************************************
void kVectorMapDof(double *v0, const double *v1, const int dof, const int j)
{
   GET_ADRS(v0);
   GET_CONST_ADRS(v1);
   forall(i, 1, d_v0[dof] = d_v1[j]; );
}

// *****************************************************************************
void kVectorSetDof(double *v0, const double alpha, const int dof)
{
   GET_ADRS(v0);
   forall(i, 1, d_v0[dof] = alpha; );
}

// *****************************************************************************
void kVectorSetDof(const int N, double *v0, const double alpha, const int *dof)
{
   GET_ADRS(v0);
   GET_CONST_ADRS_T(dof,int);
   forall(i, N,
   {
      const int dof_i = d_dof[i];
      d_v0[dof_i] = alpha;
   });
}

// *****************************************************************************
void kVectorGetSubvector(const int N,
                         double* v0,
                         const double* v1,
                         const int* v2)
{
   GET_ADRS(v0);
   GET_CONST_ADRS(v1);
   GET_CONST_ADRS_T(v2,int);
   forall(i, N,
   {
      const int dof_i = d_v2[i];
      assert(dof_i >= 0);
      d_v0[i] = dof_i >= 0 ? d_v1[dof_i] : -d_v1[-dof_i-1];
   });
}

// *****************************************************************************
void kVectorSetSubvector(const int N,
                         double* data,
                         const double* elemvect,
                         const int* dofs)
{
   GET_ADRS(data);
   GET_CONST_ADRS(elemvect);
   GET_CONST_ADRS_T(dofs,int);
   forall(i,N,
   {
      const int j = d_dofs[i];
      if (j >= 0)
      {
         d_data[j] = d_elemvect[i];
      }
      else {
         assert(false);
         d_data[-1-j] = -d_elemvect[i];
      }
   });
}

// *****************************************************************************
void kVectorSubtract(double *zp, const double *xp, const double *yp,
                     const size_t N)
{
   GET_ADRS(zp);
   GET_CONST_ADRS(xp);
   GET_CONST_ADRS(yp);
   forall(i, N, d_zp[i] = d_xp[i] - d_yp[i];);
}

// *****************************************************************************
void kVectorAlphaAdd(double *vp, const double* v1p,
                     const double alpha, const double *v2p, const size_t N)
{
   GET_ADRS(vp);
   GET_CONST_ADRS(v1p);
   GET_CONST_ADRS(v2p);
   forall(i, N, d_vp[i] = d_v1p[i] + alpha * d_v2p[i];);
}

// *****************************************************************************
void kVectorPrint(const size_t N, const double *data)
{
   GET_CONST_ADRS(data); // Sequential printf
   forall(k,1,for(size_t i=0; i<N; i+=1)printf("\n\t%f",d_data[i]););
}

// *****************************************************************************
void kVectorAssign(const size_t N, const double* v, double *data)
{
   GET_ADRS(data);
   GET_CONST_ADRS(v);
   forall(i, N, d_data[i] = d_v[i];);
}

// **************************************************************************
void kVectorSet(const size_t N,
                const double value,
                double *data)
{
   GET_ADRS(data);
   forall(i, N, d_data[i] = value;);
}

// *****************************************************************************
void kVectorMultOp(const size_t N,
                   const double value,
                   double *data)
{
   GET_ADRS(data);
   forall(i, N, d_data[i] *= value;);
}

// *****************************************************************************
void kVectorDotOpPlusEQ(const size_t size, const double *v, double *data)
{
   GET_CONST_ADRS(v);
   GET_ADRS(data);
   forall(i, size, d_data[i] += d_v[i];);
}

// *****************************************************************************
void kVectorOpSubtract(const size_t size, const double *v, double *data)
{
   GET_CONST_ADRS(v);
   GET_ADRS(data);
   forall(i, size, d_data[i] -= d_v[i];);
}

// *****************************************************************************
void kAddElementVector(const size_t n, const int *dofs,
                       const double *elem_data, double *data)
{
   GET_CONST_ADRS_T(dofs,int);
   GET_CONST_ADRS(elem_data);
   GET_ADRS(data);
   forall(i, n, {
         const int j = d_dofs[i];
         if (j >= 0)
            d_data[j] += d_elem_data[i];
         else
            d_data[-1-j] -= d_elem_data[i];
      });
}

// *****************************************************************************
MFEM_NAMESPACE_END
