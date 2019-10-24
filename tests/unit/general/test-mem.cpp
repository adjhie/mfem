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

#include "mfem.hpp"
#include "general/dbg.hpp"

#include "catch.hpp"

//#define _WIN32
#ifndef _WIN32
#include <unistd.h>
#else
#define sysconf(...) 0x1000
#endif

using namespace mfem;

static void ScanMemoryTypes(const int size = 1024)
{
   constexpr int N = static_cast<int>(MemoryType::SIZE);
   Vector v[N];
   MemoryType mt = MemoryType::HOST;
   for (int i=0; i<N; i++, mt++)
   {
      if (!Device::Allows(Backend::DEVICE_MASK) &&
          !mfem::IsHostMemory(mt)) { continue; }
      if (i==static_cast<int>(MemoryType::HOST_UMPIRE)) { continue; }
      if (i==static_cast<int>(MemoryType::DEVICE_UMPIRE)) { continue; }
      Memory<double> mem(size, mt);
      REQUIRE(mem.Capacity() == size);
      Vector &y = v[i];
      y.NewMemoryAndSize(mem, size, true);
      y.UseDevice(true);
      y.HostWrite();
      y.Write();
      y = 0.0;
      y.HostReadWrite();
      y[0] = 1.0;
      y.Destroy();
   }
}

static void MmuCatch(const int N = 1024)
{
   Vector Y(N);
   double *h_Y = (double*)Y;
   Y.UseDevice(true);
   Y = 0.0;
   // in debug device, should raise a SIGSEGV
   // but it can't be caught by this version of Catch
   //h_Y[0] = 0.0;
}

void Aliases(const int N = 0x1234)
{
   Vector S(2*3*N + N);
   S.UseDevice(true);
   S = -1.0;
   GridFunction X,V,E;
   const int Xsz = 3*N;
   const int Vsz = 3*N;
   const int Esz = N;
   X.NewMemoryAndSize(Memory<double>(S.GetMemory(), 0, Xsz), Xsz, false);
   V.NewMemoryAndSize(Memory<double>(S.GetMemory(), Xsz, Vsz), Vsz, false);
   E.NewMemoryAndSize(Memory<double>(S.GetMemory(), Xsz + Vsz, Esz), Esz, false);
   X = 1.0;
   X.SyncAliasMemory(S);
   S.HostWrite();
   X.SyncAliasMemory(S);
   S = -1.0;
   X.Write();
   X = 1.0;
   S.HostRead();
   REQUIRE(S*S == Approx(7.0*N));
   V = 2.0;
   V.SyncAliasMemory(S);
   S.HostRead();
   REQUIRE(S*S == Approx(16.0*N));
   E = 3.0;
   E.SyncAliasMemory(S);
   REQUIRE(S*S == Approx(24.0*N));
   S.Destroy();
}

TEST_CASE("MemoryManager", "[MemoryManager]")
{
   SECTION("Debug")
   {
      const long pagesize = sysconf(_SC_PAGE_SIZE);
      REQUIRE(pagesize > 0);
      Device device("debug");
      for (int n = 1; n < 3*pagesize; n+=7)
      {
         Aliases(n);
         mm.PrintPtrs();
         mm.PrintAliases();
      }
      MmuCatch();
      ScanMemoryTypes();
      mm.PrintPtrs();
      mm.PrintAliases();
   }
}
