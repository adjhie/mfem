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

#include "../general/okina.hpp"

// *****************************************************************************
namespace mfem
{

// *****************************************************************************
// * Tests if adrs is a known address
// *****************************************************************************
static bool Known(const mm_t *maps, const void *adrs)
{
   const auto found = maps->memories->find(adrs);
   const bool known = found != maps->memories->end();
   if (known) return true;
   return false;
}

// *****************************************************************************
// * Looks if adrs is an alias of one memory
// *****************************************************************************
static const void* IsAlias(const mm_t *maps, const void *adrs)
{
   MFEM_ASSERT(not Known(maps, adrs), "Adrs is an already known address!");
   for(mm_iterator_t mem = maps->memories->begin();
       mem != maps->memories->end(); mem++) {
      const void *b_adrs = mem->first;
      if (b_adrs > adrs) continue;
      const void *end = (char*)b_adrs + mem->second->bytes;
      if (adrs < end) return b_adrs;
   }
   return NULL;
}

// *****************************************************************************
static const void* InsertAlias(const mm_t *maps,
                               const void *base,
                               const void *adrs)
{
   assert(adrs > base);
   memory_t *mem = maps->memories->at(base);
   const size_t offset = (char*)adrs - (char*)base;
   const alias_t *alias = new alias_t(mem, offset);
   maps->aliases->operator[](adrs) = alias;
   mem->aliases.push_back(alias);
   return adrs;
}

// *****************************************************************************
// * Tests if adrs is an alias address
// *****************************************************************************
static bool Alias(const mm_t *maps, const void *adrs)
{
   const auto found = maps->aliases->find(adrs);
   const bool alias = found != maps->aliases->end();
   if (alias) return true;
   const void *base = IsAlias(maps, adrs);
   if (not base) return false;
   InsertAlias(maps, base, adrs);
   return true;   
}

// *****************************************************************************
// * Adds an address
// *****************************************************************************
void* mm::Insert(void *adrs, const size_t bytes)
{
   const bool known = Known(maps, adrs);
   MFEM_ASSERT(not known, "Trying to add already present address!");
   //dbg("\033[33m%p \033[35m(%ldb)", adrs, bytes);
   memories->operator[](adrs) = new memory_t(adrs,bytes);
   return adrs;
}

// *****************************************************************************
// * Remove the address from the map, as well as all the address' aliases
// *****************************************************************************
void *mm::Erase(void *adrs)
{
   const bool known = Known(maps, adrs);
   if (not known) { BUILTIN_TRAP; }
   MFEM_ASSERT(known, "Trying to remove an unknown address!");
   const memory_t *mem = memories->at(adrs);
   for (const alias_t* const alias : mem->aliases) {
      aliases->erase(alias);
      delete alias;
   }
   memories->erase(adrs);
   return adrs;
}

// *****************************************************************************
static void* AdrsKnown(const mm_t *maps, void* adrs){
   memory_t *base = maps->memories->at(adrs);
   const bool host = base->host;
   const size_t bytes = base->bytes;
   const bool device = not base->host;
   const bool cuda = config::Cuda();
   if (host and not cuda) { return adrs; }
   if (not base->d_adrs) { okMemAlloc(&base->d_adrs, bytes); }
   if (device and cuda) { return base->d_adrs; }
   // Pull
   if (device and not cuda)
   {
      okMemcpyDtoH(adrs, base->d_adrs, bytes);
      base->host = true;
      return adrs;
   }
   // Push
   assert(host and cuda);
   okMemcpyHtoD(base->d_adrs, adrs, bytes);
   base->host = false;
   return base->d_adrs;   
}

// *****************************************************************************
static void* AdrsAlias(mm_t *maps, void* adrs){
   const bool cuda = config::Cuda();
   const alias_t *alias = maps->aliases->at(adrs);
   memory_t *base = alias->mem;
   const bool host = base->host;
   const bool device = not base->host;
   const size_t bytes = base->bytes;
   if (host and not cuda) { return adrs; }
   if (not base->d_adrs) { okMemAlloc(&base->d_adrs, bytes); }
   void *d_adrs = base->d_adrs;
   void *a_adrs = (char*)d_adrs + alias->offset;
   if (device and cuda) { return a_adrs; }
   // Pull
   if (device and not cuda)
   {
      okMemcpyDtoH(base->h_adrs, d_adrs, bytes);
      base->host = true;
      return adrs;
   }
   // Push
   assert(host and cuda);
   okMemcpyHtoD(d_adrs, base->h_adrs, bytes);
   base->host = false;
   return a_adrs;
}

// *****************************************************************************
// * Turn an address to the right host or device one
// *****************************************************************************
void* mm::Adrs(void *adrs)
{
   if (not config::Nvcc()) return adrs;
   if (not config::Cuda()) return adrs;
   if (Known(maps, adrs)) return AdrsKnown(maps, adrs);
   const bool alias = Alias(maps, adrs);
   if (not alias) { BUILTIN_TRAP; }   
   MFEM_ASSERT(alias, "Unknown address!");
   return AdrsAlias(maps, adrs);
   /*
   //const bool occa = config::Occa();
   // If it hasn't been seen, alloc it in the device
   if (is_not_device_ready and occa)
   {
   assert(false);
   dbg("is_not_device_ready and OCCA");
   const size_t bytes = mm.bytes;
   if (bytes>0) { okMemAlloc(&mm.d_adrs, bytes); }
   void *stream = config::Stream();
   okMemcpyHtoDAsync(mm.d_adrs, mm.h_adrs, bytes, stream);
   mm.host = false; // This address is no more on the host
   }
   */
}

// *****************************************************************************
const void* mm::Adrs(const void *adrs){
   return (const void*) Adrs((void*)adrs);
}

// *****************************************************************************
static void PushKnown(mm_t *maps, const void *adrs, const size_t bytes)
{
   memory_t *base = maps->memories->at(adrs);
   if (not base->d_adrs){ okMemAlloc(&base->d_adrs, base->bytes); }
   okMemcpyHtoD(base->d_adrs, adrs, bytes==0?base->bytes:bytes);
}

// *****************************************************************************
static void PushAlias(const mm_t *maps, const void *adrs, const size_t bytes)
{
   const alias_t *alias = maps->aliases->at(adrs);
   memory_t *base = alias->mem;
   //assert(base->d_adrs);
   if (not base->d_adrs){ okMemAlloc(&base->d_adrs, base->bytes); }
   okMemcpyHtoD((char*)base->d_adrs + alias->offset, adrs, bytes);
}

// *****************************************************************************
void mm::Push(const void *adrs, const size_t bytes)
{
   if (not config::Nvcc()) return;
   if (not config::Cuda()) return;
   if (Known(maps, adrs)) return PushKnown(maps, adrs, bytes);
   const bool alias = Alias(maps, adrs);
   if (not alias) { BUILTIN_TRAP; }
   MFEM_ASSERT(alias, "Unknown address!");
   return PushAlias(maps, adrs, bytes);
}

// *****************************************************************************
static void PullKnown(const mm_t *maps, const void *adrs, const size_t bytes)
{
   const memory_t *base = maps->memories->at(adrs);
   okMemcpyDtoH(base->h_adrs, base->d_adrs, bytes==0?base->bytes:bytes);
}

// *****************************************************************************
static void PullAlias(const mm_t *maps, const void *adrs, const size_t bytes)
{
   const alias_t *alias = maps->aliases->at(adrs);
   const memory_t *base = alias->mem;
   okMemcpyDtoH((void*)adrs, (char*)base->d_adrs + alias->offset, bytes);
}

// *****************************************************************************
void mm::Pull(const void *adrs, const size_t bytes)
{
   if (not config::Nvcc()) return;
   if (not config::Cuda()) return;
   if (Known(maps, adrs)) return PullKnown(maps, adrs, bytes);
   const bool alias = Alias(maps, adrs);
   if (not alias) { BUILTIN_TRAP; }
   MFEM_ASSERT(alias, "Unknown address!");
   return PullAlias(maps, adrs, bytes);
   // if (config::Occa()) { okCopyTo(Memory(adrs), (void*)mm.h_adrs);}
}

// *****************************************************************************
__attribute__((unused))
static OccaMemory Memory(const mm_t *maps, const void *adrs)
{
   const bool present = Known(maps, adrs);
   if (not present) { BUILTIN_TRAP; }
   MFEM_ASSERT(present, "Trying to convert unknown address!");
   const bool occa = config::Occa();
   MFEM_ASSERT(occa, "Using OCCA memory without OCCA mode!");
   memory_t *mem = maps->memories->at(adrs);
   const bool cuda = config::Cuda();
   const size_t bytes = mem->bytes;
   OccaDevice device = config::OccaGetDevice();
   if (not mem->d_adrs)
   {
      mem->host = false; // This address is no more on the host
      if (cuda)
      {
         okMemAlloc(&mem->d_adrs, bytes);
         void *stream = config::Stream();
         okMemcpyHtoDAsync(mem->d_adrs, mem->h_adrs, bytes, stream);
      }
      else
      {
         mem->o_adrs = okDeviceMalloc(device, bytes);
         mem->d_adrs = okMemoryPtr(mem->o_adrs);
         okCopyFrom(mem->o_adrs, mem->h_adrs);
      }
   }
   if (cuda)
   {
      return okWrapMemory(device, mem->d_adrs, bytes);
   }
   return mem->o_adrs;
}

// *****************************************************************************
__attribute__((unused))
static void Dump(mm_t *maps){
   if (!getenv("DBG")) return;
   memory_map_t *mem = maps->memories;
   alias_map_t  *als = maps->aliases;
   size_t k = 0;
   for(memory_map_t::iterator m = mem->begin(); m != mem->end(); m++) {
      const void *h_adrs = m->first;
      assert(h_adrs == m->second->h_adrs);
      const size_t bytes = m->second->bytes;
      const void *d_adrs = m->second->d_adrs;
      if (not d_adrs){
         printf("\n[%ld] \033[33m%p \033[35m(%ld)", k, h_adrs, bytes);
      }else{
         printf("\n[%ld] \033[33m%p \033[35m (%ld) \033[32 -> %p", k, h_adrs, bytes, d_adrs);
      }
      fflush(0);
      k++;
   }
   k = 0;
   for(alias_map_t::iterator a = als->begin(); a != als->end(); a++) {
      const void *adrs = a->first;
      const size_t offset = a->second->offset;
      const void *base = a->second->mem->h_adrs;
      printf("\n[%ld] \033[33m%p < (\033[37m%ld) < \033[33m%p",k , base, offset, adrs);
      fflush(0);
      k++;
   }
}

// *****************************************************************************
// * Data will be pushed/pulled before the copy happens on the H or the D
// *****************************************************************************
static void* d2d(void *dst, const void *src, size_t bytes, const bool async){
   GET_ADRS(src);
   GET_ADRS(dst);
   assert(bytes>0);
   const bool cuda = config::Cuda();
   if (not cuda) { return std::memcpy(d_dst, d_src, bytes); }
   if (not async) { okMemcpyDtoD(d_dst, (void*)d_src, bytes); }
   else { okMemcpyDtoDAsync(d_dst, (void*)d_src, bytes, config::Stream()); }
   return dst;
}

// *****************************************************************************
void* mm::memcpy(void *dst, const void *src, size_t bytes, const bool async)
{
   if (bytes==0) { return dst; }
   return d2d(dst, src, bytes, async);
}

// *****************************************************************************
} // namespace mfem
