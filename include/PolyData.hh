/*=========================================================================

  Program:   Visualization Toolkit
  Module:    PolyData.hh
  Language:  C++
  Date:      $Date$
  Version:   $Revision$


Copyright (c) 1993-1995 Ken Martin, Will Schroeder, Bill Lorensen.

This software is copyrighted by Ken Martin, Will Schroeder and Bill Lorensen.
The following terms apply to all files associated with the software unless
explicitly disclaimed in individual files. This copyright specifically does
not apply to the related textbook "The Visualization Toolkit" ISBN
013199837-4 published by Prentice Hall which is covered by its own copyright.

The authors hereby grant permission to use, copy, and distribute this
software and its documentation for any purpose, provided that existing
copyright notices are retained in all copies and that this notice is included
verbatim in any distributions. Additionally, the authors grant permission to
modify this software and its documentation for any purpose, provided that
such modifications are not distributed without the explicit consent of the
authors and that existing copyright notices are retained in all copies. Some
of the algorithms implemented by this software are patented, observe all
applicable patent law.

IN NO EVENT SHALL THE AUTHORS OR DISTRIBUTORS BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
OF THE USE OF THIS SOFTWARE, ITS DOCUMENTATION, OR ANY DERIVATIVES THEREOF,
EVEN IF THE AUTHORS HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

THE AUTHORS AND DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE, AND NON-INFRINGEMENT.  THIS SOFTWARE IS PROVIDED ON AN
"AS IS" BASIS, AND THE AUTHORS AND DISTRIBUTORS HAVE NO OBLIGATION TO PROVIDE
MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.


=========================================================================*/
// .NAME vtkPolyData - concrete dataset represents vertices, lines, polygons, and triangle strips
// .SECTION Description
// vtkPolyData is a data object that is a concrete implementation of vtkDataSet.
// vtkPolyData represents a geometric structure consisting of vertices, lines,
// polygons, and triangle strips. Point attribute values (e.g., scalars,
// vectors, etc.) are also represented.
//
// The actual cell types (CellType.hh) supported by vtkPolyData are: vtkVERTEX,
// vtkPOLY_VERTEX, vtkLINE, vtkPOLYLINE, vtkTRIANGLE, vtkTRIANGLE_STRIP,
// vtkPOLYGON, vtkRECTANGLE, and vtkQUAD.

#ifndef __vtkPolyData_h
#define __vtkPolyData_h

#include "PointSet.hh"
#include "FPoints.hh"
#include "CellArr.hh"
#include "CellList.hh"
#include "LinkList.hh"

class vtkPolyData : public vtkPointSet 
{
public:
  vtkPolyData();
  vtkPolyData(const vtkPolyData& pd);
  ~vtkPolyData();
  char *GetClassName() {return "vtkPolyData";};
  char *GetDataType() {return "vtkPolyData";};
  void PrintSelf(ostream& os, vtkIndent indent);

  // dataset interface
  vtkDataSet *MakeObject() {return new vtkPolyData(*this);};
  int GetNumberOfCells();
  vtkCell *GetCell(int cellId);
  int GetCellType(int cellId);
  void GetCellPoints(int cellId, vtkIdList& ptIds);
  void GetPointCells(int ptId, vtkIdList& cellIds);

  // Can't use macros to set/get following cell arrays.  This is due to tricks
  // required to support traversal methods.
  void SetVerts (vtkCellArray* v);
  vtkCellArray *GetVerts();

  void SetLines (vtkCellArray* l);
  vtkCellArray *GetLines();

  void SetPolys (vtkCellArray* p);
  vtkCellArray *GetPolys();

  void SetStrips (vtkCellArray* s);
  vtkCellArray *GetStrips();

  int GetNumberOfVerts();
  int GetNumberOfLines();
  int GetNumberOfPolys();
  int GetNumberOfStrips();

  // Allocate storage for cells when using the following InsertNextCell method
  void Allocate(int numCells=1000, int extSize=1000);
  // create verts, lines, polys, tmeshes from cell object
  int InsertNextCell(int type, int npts, int pts[MAX_CELL_SIZE]);
  // Use this method to reclaim memory when using InsertNextCell()
  void Squeeze();

  // construct adjacency structure
  void BuildCells();
  void BuildLinks();

  // Special (efficient) operations on poly data. Use carefully.
  void GetPointCells(int ptId, unsigned short& ncells, int* &cells);
  void GetCellEdgeNeighbors(int cellId, int p1, int p2, vtkIdList& cellIds);
  void GetCellPoints(int cellId, int& npts, int* &pts);
  int IsTriangle(int v1, int v2, int v3);
  int IsEdge(int v1, int v2);
  int IsPointUsedByCell(int ptId, int cellId);
  void ReplaceCell(int cellId, int npts, int *pts);
  void ReverseCell(int cellId);
  void DeletePoint(int ptId);
  void DeleteCell(int cellId);
  void ReplaceLinkedCell(int cellId, int npts, int *pts);
  void RemoveCellReference(int cellId);
  void ResizeCellList(int ptId, int size);

protected:
  void Initialize();

  // points inherited
  // point data (i.e., scalars, vectors, normals, tcoords) inherited
  vtkCellArray *Verts;
  vtkCellArray *Lines;
  vtkCellArray *Polys;
  vtkCellArray *Strips;

  // dummy static member below used as a trick to simplify traversal
  static vtkCellArray *Dummy;

  // supporting structures for more complex topological operations
  // built only when necessary
  vtkCellList *Cells;
  vtkLinkList *Links;

};

// Description:
// Efficient method to obtain cells using a particular point. Make sure that
// routine BuildLinks() has been called.
inline void vtkPolyData::GetPointCells(int ptId, unsigned short& ncells, 
                                      int* &cells)
{
  ncells = this->Links->GetNcells(ptId);
  cells = this->Links->GetCells(ptId);
}

// Description:
// Given three vertices, determine whether it's a triangle. Make sure 
// BuildLinks() has been called first.
inline int vtkPolyData::IsTriangle(int v1, int v2, int v3)
{
  unsigned short int n1;
  int i, j, n2, *cells, tVerts[3], *tVerts2;

  tVerts[0] = v1;
  tVerts[1] = v2;
  tVerts[2] = v3;

  for (i=0; i<3; i++) 
    {
    this->GetPointCells(tVerts[i], n1, cells);
    for (j=0; j<n1; j++) 
      {
      this->GetCellPoints(cells[j], n2, tVerts2);
      if ( (tVerts[0] == tVerts2[0] || tVerts[0] == tVerts2[1] ||
      tVerts[0] == tVerts2[2]) &&
      (tVerts[1] == tVerts2[0] || tVerts[1] == tVerts2[1] ||
      tVerts[1] == tVerts2[2]) &&
      (tVerts[2] == tVerts2[0] || tVerts[2] == tVerts2[1] ||
      tVerts[2] == tVerts2[2]) )
        return 1;
      }
    }
  return 0;
}

// Description:
// Determine whether a point is used by a particular cell. If it is, return
// non-zero. Make sure BuildCells() has been called first.
inline int vtkPolyData::IsPointUsedByCell(int ptId, int cellId)
{
  int npts, *pts;
  this->GetCellPoints(cellId, npts, pts);
  for (int i=0; i < npts; i++)
    if ( pts[i] == ptId ) return 1;

  return 0;
}

// Description:
// Determine whether two points form an edge. If they do, return non-zero.
// Make sure BuildLinks() has been called first.
inline int vtkPolyData::IsEdge(int p1, int p2)
{
  unsigned short int ncells;
  int i, *cells;

  this->GetPointCells(p1,ncells,cells);
  for (i=0; i < ncells; i++)
    if ( this->IsPointUsedByCell(p2,cells[i]) ) return 1;

  return 0;
}

inline void vtkPolyData::DeletePoint(int ptId)
{
  this->Links->DeletePoint(ptId);
}

inline void vtkPolyData::DeleteCell(int cellId)
{
  this->Cells->DeleteCell(cellId);
}

inline void vtkPolyData::RemoveCellReference(int cellId)
{
  int npts, *pts;
  this->GetCellPoints(cellId, npts, pts);
  for (int i=0; i<npts; i++)
    this->Links->RemoveCellReference(cellId, pts[i]);  
}

inline void vtkPolyData::ResizeCellList(int ptId, int size)
{
  this->Links->ResizeCellList(ptId,size);
}

#endif


