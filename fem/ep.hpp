// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.googlecode.com.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#ifndef MFEM_EP
#define MFEM_EP

#include <cassert>

#include "../config/config.hpp"

extern "C" void
dsptrf_(char *, int *, double *, int *, int *);
extern "C" void
dsptri_(char *, int *, double *, int *, double *,int *);

#ifdef MFEM_USE_MPI

#include <mpi.h>
#include "../linalg/hypre.hpp"
#include "pfespace.hpp"
#include "bilinearform.hpp"

#endif // MFEM_USE_MPI

namespace mfem {

#ifdef MFEM_USE_MPI
/*
class MyHypreParVector : public HypreParVector {
private:
  MPI_Comm comm_;
public:
  MyHypreParVector(MPI_Comm comm, int glob_size, int *col);
  MyHypreParVector(ParFiniteElementSpace *pfes);

  double Norml2();

  double Normlinf();
};
*/
#endif // MFEM_USE_MPI

class EPDoFs
{
private:
  FiniteElementSpace * fes_;
  // int nExposedDofs_;
  // int nPrivateDofs_;

  Table * expDoFsByElem_;
  int   * priOffset_;

protected:
public:
  EPDoFs(FiniteElementSpace & fes);

  ~EPDoFs();

  inline FiniteElementSpace * FESpace() const { return fes_; }

  inline int GetNDofs()        { return fes_->GetNDofs(); }
  inline int GetNElements()    { return fes_->GetNE(); }
  inline int GetNExposedDofs() { return fes_->GetNExDofs(); }
  inline int GetNPrivateDofs() { return fes_->GetNPrDofs(); }
  // inline int GetNExposedDofs() { return nExposedDofs_; }
  // inline int GetNPrivateDofs() { return nPrivateDofs_; }

  /*
  void BuildElementToDofTable();

  void GetElementDofs(const int elem,
		      Array<int> & ExpDoFs);

  void GetElementDofs(const int elem,
		      Array<int> & ExpDoFs,
		      int & PriOffset, int & numPri);

  inline const int * GetPrivateOffsets() const { return priOffset_; }
  */
  void GetElementDofs(const int elem,
		      Array<int> & ExpDoFs)
  {
    fes_->GetElementDofs(elem,ExpDoFs);
  }

  void GetElementDofs(const int elem,
		      Array<int> & ExpDoFs,
		      int & PriOffset, int & numPri)
  {
    fes_->GetElementDofs(elem,ExpDoFs,PriOffset,numPri);
  }

  inline const int * GetPrivateOffsets() const
  { return fes_->GetPrivateOffsets(); }

};

#ifdef MFEM_USE_MPI

class ParEPDoFs /*: public EPDoFs*/
{
private:
  ParFiniteElementSpace * pfes_;
  HypreParMatrix        * Pe_;

  // int   nParExposedDofs_;
  // int * ExposedPart_;
  // int * TExposedPart_;

protected:
public:
  ParEPDoFs(ParFiniteElementSpace & pfes);

  ~ParEPDoFs();

  inline ParFiniteElementSpace * PFESpace() const { return pfes_; }

  inline HypreParMatrix * EDof_TrueEDof_Matrix() { return Pe_; }
  inline HypreParMatrix * ExDof_TrueExDof_Matrix() { return pfes_->ExDof_TrueExDof_Matrix(); }

  inline MPI_Comm GetComm()            { return pfes_->GetComm(); }
  inline int      GetNRanks()          { return pfes_->GetNRanks(); }
  // inline int      GetNParExposedDofs() { return nParExposedDofs_; }
  inline int      TrueExVSize()        { return pfes_->TrueExVSize(); }
  inline int      GetNExDofs()         { return pfes_->GetNExDofs(); }
  inline int      GetNPrDofs()         { return pfes_->GetNPrDofs(); }
  // inline int *    GetPartitioning()    { return ExposedPart_; }
  // inline int *    GetTPartitioning()   { return TExposedPart_; }
  // int GlobalNExposedDofs();
  // int GlobalNTrueExposedDofs();
};

#endif // MFEM_USE_MPI
/*
class EPField : protected Vector
{
protected:
  unsigned int numFields_;

private:
  EPDoFs *  epdofs_;

  Vector ** ExposedDoFs_;
  Vector ** PrivateDoFs_;

  void initVectors(const unsigned int num = 1);

public:
  EPField(EPDoFs & epdofs);

  ~EPField();

  inline int GetNFields() const { return numFields_; }

  double Norml2();

  EPField & operator-=(const EPField &v);

  void initFromInterleavedVector(const Vector & x);

  const Vector * ExposedDoFs(const unsigned int i = 0) const;

  Vector * ExposedDoFs(const unsigned int i = 0);

  const Vector * PrivateDoFs(const unsigned int i = 0) const;

  Vector * PrivateDoFs(const int unsigned i = 0);
};

#ifdef MFEM_USE_MPI

class ParEPField : public EPField
{
private:
  ParEPDoFs * pepdofs_;
  MyHypreParVector ** ParExposedDoFs_;

  void initVectors(const unsigned int num = 1);

protected:
public:
  ParEPField(ParEPDoFs & pepdofs);

  ~ParEPField();

  void updateParExposedDoFs();

  void updateExposedDoFs();

  double Norml2();

  double Normlinf();

  ParEPField & operator-=(const ParEPField &v);

  void initFromInterleavedVector(const HypreParVector & x);

  const MyHypreParVector * ParExposedDoFs(const unsigned int i=0) const;

  MyHypreParVector * ParExposedDoFs(const unsigned int i=0);

};

#endif // MFEM_USE_MPI
*/
/*
class BlockDiagonalMatrixInverse;

class BlockDiagonalMatrix : public Matrix
{
  friend class BlockDiagonalMatrixInverse;
private:
  DenseMatrix ** blocks_;
  const int    * blockOffsets_;
  int            nBlocks_;

public:
  BlockDiagonalMatrix(const int nBlocks, const int * blockOffsets);

  ~BlockDiagonalMatrix();

  inline void Finalize(int) {}

  inline DenseMatrix * GetBlock(const int i) { return blocks_[i]; }
};
*/
class EPBilinearForm : public Operator
{
private:
  // EPDoFs * epdofsL_;
  // EPDoFs * epdofsR_;
  FiniteElementSpace * epdofsL_;
  FiniteElementSpace * epdofsR_;

  BilinearFormIntegrator * bfi_;

  SparseMatrix  *  Mee_;
  SparseMatrix  *  Mep_;
  SparseMatrix  *  Mpe_;
  SparseMatrix  *  Mrr_;
  DenseMatrix   ** Mpp_;
  DenseMatrixInverse ** MppInv_;

  Vector        *  reducedRHS_;
  Vector        *  vecp_;

protected:

  void buildReducedRHS(const Vector & bExp, const Vector & bPri) const;

public:
  EPBilinearForm(/*EPDoFs & epdofsL, EPDoFs & epdofsR,*/
		 FiniteElementSpace & epdofsL, FiniteElementSpace & epdofsR,
		 BilinearFormIntegrator & bfi);

  ~EPBilinearForm();

  void Assemble();

  void Finalize();

  inline SparseMatrix * GetMee() const { return Mee_; }
  inline SparseMatrix * GetMep() const { return Mep_; }
  inline SparseMatrix * GetMpe() const { return Mpe_; }
  inline SparseMatrix * GetMrr() const { return Mrr_; }
  inline DenseMatrix ** GetMpp() const { return Mpp_; }
  inline DenseMatrixInverse ** GetMppInv() const { return MppInv_; }

  // void Mult(const EPField & x, EPField & y) const;
  void Mult(const Vector & x, Vector & y) const;
  void Mult(const Vector & xE, const Vector & xP,
	    Vector & yE, Vector & yP) const;

  const Vector * ReducedRHS(const Vector & bExp, const Vector & bPri) const;
  const Vector * ReducedRHS(const Vector & b) const;
  // const Vector * ReducedRHS(const EPField & b) const;
  const Vector * ReducedRHS() const;

  void SolvePrivateDoFs(const Vector & b, Vector & x) const;
  // void SolvePrivateDoFs(const Vector & bP, EPField & x) const;
  void SolvePrivateDoFs(const Vector & bP, const Vector & xE,
			Vector & xP) const;

  void EliminateEssentialBC(Array<int> &bdr_attr_is_ess,
			    Vector & x, Vector & b, int d = 0);

  // void EliminateEssentialBCFromDofs(Array<int> &bdr_attr_is_ess,
  //			    EPField & sol, EPField & rhs, int d = 0);

  void EliminateEssentialBCFromDofs(Array<int> &bdr_attr_is_ess,
				    Vector & x, Vector & b, int d = 0);

  void EliminateEssentialBCFromDofs(Array<int> &bdr_attr_is_ess,
				    Vector & xE, Vector & bE, Vector & bP,
				    int d = 0);
};

#ifdef MFEM_USE_MPI

class ParEPBilinearForm : public EPBilinearForm
{
protected:

  class ParReducedOp : public Operator {
  private:
    // ParEPDoFs      * pepdofs_;
    ParFiniteElementSpace * pepdofs_;
    HypreParMatrix * HypreMrr_;
    HypreParMatrix * ParMrr_;
    HypreParMatrix * Pe_;

  public:
    ParReducedOp(/*ParEPDoFs*/ParFiniteElementSpace * pepdofs, SparseMatrix * Mrr)
      : pepdofs_(pepdofs),
	HypreMrr_(NULL),
	ParMrr_(NULL),
	Pe_(NULL)
    {
      Operator::width = pepdofs_->TrueExVSize();

      Pe_  = pepdofs_->ExDof_TrueExDof_Matrix();

      HypreMrr_ = new HypreParMatrix(Pe_->GetComm(),
				     Pe_->M(),
				     Pe_->RowPart(),Mrr);
    }

    ~ParReducedOp()
    {
      if ( HypreMrr_ != NULL ) delete HypreMrr_;
      if ( ParMrr_   != NULL ) delete ParMrr_;
    }

    void Finalize()
    {
      ParMrr_ = RAP(HypreMrr_,Pe_);
    }

    // The following returns the reduced matrices from each process
    // before combining shared DoFs
    HypreParMatrix * ReducedDoFMat() { return HypreMrr_; }

    // The following returns the final reduced matrix
    HypreParMatrix * ReducedMat() { return ParMrr_; }

    inline void Mult(const Vector & x, Vector & y) const
    {
      ParMrr_->Mult(x,y);
    }
  };

private:
  /*
  ParEPDoFs      * pepdofsL_;
  ParEPDoFs      * pepdofsR_;
  */
  ParFiniteElementSpace * pepdofsL_;
  ParFiniteElementSpace * pepdofsR_;
  ParReducedOp   * preducedOp_;
  HypreParVector * preducedRHS_;
  Vector         * vec_;
  Vector         * vecp_;

public:
  ParEPBilinearForm(/*ParEPDoFs*/ParFiniteElementSpace & pepdofsL,
		    /*ParEPDoFs*/ParFiniteElementSpace & pepdofsR,
		    BilinearFormIntegrator & bfi);

  ~ParEPBilinearForm();

  void Assemble();

  void Finalize();

  // void Mult(const ParEPField & x, ParEPField & y) const;

  const Operator * ReducedOperator() const;

  HypreParMatrix * ReducedMat() { return preducedOp_->ReducedMat(); }

  const HypreParVector * ReducedRHS(const Vector & b) const;
  // const HypreParVector * ReducedRHS(const ParEPField & x) const;
  const HypreParVector * ReducedRHS() const;

};

#endif // MFEM_USE_MPI
/*
template<class Solver>
class EPSolver :
public virtual Solver
{
private:
  const EPBilinearForm * epMat_;
protected:
public:
  EPSolver() : Solver(), epMat_(NULL) {}
  ~EPSolver() {}

  void SetOperator(const EPBilinearForm & A)
  {
    epMat_ = &A;

    this->Solver::SetOperator(*epMat_->GetMrr());
  }

  void Mult (const EPField & x, EPField & y) const
  {
    assert( epMat_ != NULL  );

    this->Solver::Mult(*epMat_->ReducedRHS(x),*y.ExposedDoFs());

    epMat_->SolvePrivateDoFs(*x.PrivateDoFs(),y);
  }

};

#ifdef MFEM_USE_MPI

template<class Solver>
class ParEPSolver: public EPSolver<Solver> {
private:
  const ParEPBilinearForm * pepMat_;
protected:
public:

  ParEPSolver(MPI_Comm _comm)
    : EPSolver<Solver>(),
      Solver(_comm),
      pepMat_(NULL) {}

  void SetOperator(const ParEPBilinearForm & A)
  {
    pepMat_ = &A;

    this->Solver::SetOperator(*pepMat_->ReducedOperator());
  }

  ~ParEPSolver() {}

  void Mult (const ParEPField & x, ParEPField & y) const
  {
    assert( pepMat_ != NULL  );

    this->Solver::Mult(*pepMat_->ReducedRHS(x),*y.ParExposedDoFs());

    y.updateExposedDoFs();

    pepMat_->SolvePrivateDoFs(*x.PrivateDoFs(),y);
  }
};

#endif // MFEM_USE_MPI
*/
}

#endif // MFEM_EP
