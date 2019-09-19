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
#ifndef MFEM_JIT_HPP
#define MFEM_JIT_HPP

#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <unordered_map>

// *****************************************************************************
typedef union {double d; uint64_t u;} union_du;

// *****************************************************************************
namespace mfem
{

namespace jit
{

// default jit::hash ***********************************************************
template<typename T> struct hash
{ size_t operator()(const T &o) const noexcept { return std::hash<T> {}(o); } };

// const char* specialization **************************************************
static size_t hash_bytes(const char *s, size_t i) noexcept
{
   size_t hash = 0xcbf29ce484222325ull;
   constexpr size_t prime = 0x100000001b3ull;
   for (; i; i--) { hash = (hash*prime)^static_cast<size_t>(s[i]); }
   return hash;
}
template<> struct hash<const char*>
{
   size_t operator()(const char *s) const noexcept
   { return hash_bytes(s, strlen(s)); }
};

// *****************************************************************************
// * Hash functions to combine the arguments
// *****************************************************************************
template <class T>
inline size_t hash_combine(const size_t &seed, const T &v) noexcept
{ return seed^(jit::hash<T> {}(v)+0x9e3779b9ull+(seed<<6)+(seed>>2)); }
template<typename T>
size_t hash_args(const size_t &seed, const T &that) noexcept
{ return hash_combine(seed, that); }
template<typename T, typename... Args>
size_t hash_args(const size_t &seed, const T &first, Args... args) noexcept
{ return hash_args(hash_combine(seed, first), args...); }

// *****************************************************************************
// * Fast uint64 to char*
// *****************************************************************************
inline void uint32str(uint64_t x, char *s, const size_t offset)
{
   x=((x&0xFFFFull)<<32)|((x&0xFFFF0000ull)>>16);
   x=((x&0x0000FF000000FF00ull)>>8)|(x&0x000000FF000000FFull)<<16;
   x=((x&0x00F000F000F000F0ull)>>4)|(x&0x000F000F000F000Full)<<8;
   const uint64_t mask = ((x+0x0606060606060606ull)>>4)&0x0101010101010101ull;
   x|=0x3030303030303030ull;
   x+=0x27ull*mask;
   memcpy(s+offset,&x,sizeof(x));
}
inline void uint64str(uint64_t num, char *s, const size_t offset =1)
{ uint32str(num>>32, s, offset); uint32str(num&0xFFFFFFFFull, s+8, offset); }

// *****************************************************************************
// * compile
// *****************************************************************************
template<typename... Args>
const char *compile(const bool dbg, const size_t hash, const char *cxx,
                    const char *src, const char *mfem_build_flags,
                    const char *mfem_install_dir, Args... args)
{
   char so[21] = "k0000000000000000.so";
   char cc[21] = "k0000000000000000.cc";
   uint64str(hash, so);
   uint64str(hash, cc);
   const int fd = open(cc, O_CREAT|O_RDWR,S_IRUSR|S_IWUSR);
   assert(fd>=0);
   dprintf(fd, src, hash, args...);
   close(fd);
   constexpr size_t SZ = 4096;
   char command[SZ];
   const char *CCFLAGS = mfem_build_flags;
   const char *NVFLAGS = mfem_build_flags;
#if defined(__clang__) && (__clang_major__ > 6)
   const char *CLANG_FLAGS = "-Wno-gnu-designator -fPIC -L.. -lmfem";
#else
   const char *CLANG_FLAGS = "-fPIC";
#endif
   const bool clang = strstr(cxx, "clang");
   const bool nvcc = strstr(cxx, "nvcc");
   const char *xflags = nvcc ? NVFLAGS : clang ? CLANG_FLAGS : CCFLAGS;
   const char *xlinker = nvcc ? "-Xlinker=" : "-Wl,";
   if (snprintf(command, SZ,
                "%s %s -shared -I%s/include -o %s %s -L%s/lib %s-rpath,%s/lib -lmfem",
                cxx, xflags, mfem_install_dir, so, cc,
                mfem_install_dir, xlinker, mfem_install_dir)<0)
   { return NULL; }
   if (dbg) { printf("\033[32;1m%s\033[m\n", command); }
   if (system(command)<0) { return NULL; }
   if (!dbg) { unlink(cc); }
   return src;
}

// *****************************************************************************
// * lookup
// *****************************************************************************
template<typename... Args>
void *lookup(const bool dbg, const size_t hash, const char *cxx,
             const char *src, const char *flags, const char *dir, Args... args)
{
   char so[21] = "k0000000000000000.so";
   uint64str(hash, so);
   const int dlflags = RTLD_LAZY; // | RTLD_LOCAL;
   void *handle = dlopen(so, dlflags);
   if (!handle && !compile(dbg, hash, cxx, src, flags, dir, args...))
   { return NULL; }
   if (!(handle=dlopen(so, dlflags))) { return NULL; }
   return handle;
}

// *****************************************************************************
// * getSymbol
// *****************************************************************************
template<typename kernel_t>
inline kernel_t getSymbol(const bool dbg, const size_t hash, void *handle)
{
   char symbol[18] = "k0000000000000000";
   uint64str(hash, symbol);
   kernel_t address = (kernel_t) dlsym(handle, symbol);
   if (dbg && !address) { printf("\033[31;1m%s\033[m\n",dlerror()); fflush(0); }
   assert(address);
   return address;
}

// *****************************************************************************
// * MFEM JIT Compilation
// *****************************************************************************
template<typename kernel_t> class kernel
{
private:
   bool dbg;
   size_t seed, hash;
   void *handle;
   kernel_t code;
public:
   template<typename... Args>
   kernel(const char *cxx, const char *src, const char *flags,
          const char* dir, Args... args):
      dbg(!!getenv("MFEM_DBG")||!!getenv("DBG")||!!getenv("dbg")),
      seed(jit::hash<const char*>()(src)),
      hash(hash_args(seed, cxx, flags, dir, args...)),
      handle(lookup(dbg, hash, cxx, src, flags, dir, args...)),
      code(getSymbol<kernel_t>(dbg, hash, handle)) { }
   template<typename... Args>
   void operator_void(Args... args) { code(args...); }
   template<typename return_t,typename... Args>
   return_t operator()(const return_t rtn, Args... args)
   { return code(rtn,args...); }
   ~kernel() { dlclose(handle); }
};

} // namespace jit

} // namespace mfem

#endif // MFEM_JIT_HPP
