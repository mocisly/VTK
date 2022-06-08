#include "vtkFiniteElementFieldDistributor.h"

#include "vtkCellArray.h"
#include "vtkCellArrayIterator.h"
#include "vtkCellData.h"
#include "vtkCellType.h"
#include "vtkDataAssembly.h"
#include "vtkDoubleArray.h"
#include "vtkHexahedron.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkLagrangeCurve.h"
#include "vtkLagrangeHexahedron.h"
#include "vtkLagrangeQuadrilateral.h"
#include "vtkLagrangeTetra.h"
#include "vtkLagrangeTriangle.h"
#include "vtkLagrangeWedge.h"
#include "vtkLine.h"
#include "vtkLogger.h"
#include "vtkObjectFactory.h"
#include "vtkPartitionedDataSet.h"
#include "vtkPartitionedDataSetCollection.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkQuad.h"
#include "vtkStringArray.h"
#include "vtkTetra.h"
#include "vtkTriangle.h"
#include "vtkUnstructuredGrid.h"
#include "vtkVectorBasisLagrangeProducts.h"
#include "vtkWedge.h"

#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{

std::string GetEdgeCoeffArrName(const std::string& name)
{
  return std::string("EDGE_COEFF_") + name;
}

std::string GetFaceCoeffArrName(const std::string& name)
{
  return std::string("FACE_COEFF_") + name;
}

struct vtkFiniteElementSpec
{
  std::unordered_set<std::string> Fields;
  VTKCellType RefElement;
};

vtkDataArray* InitializeNewArray(
  vtkDataArray* in, const std::string& name, const int& ncomp, const vtkIdType& ntup)
{
  auto arr = in->NewInstance();
  arr->SetName(name.c_str());
  arr->SetNumberOfComponents(ncomp);
  arr->SetNumberOfTuples(ntup);
  arr->Fill(0.);
  return arr;
}

std::vector<std::string> Split(const std::string& inString, const std::string& delimeter)
{
  std::vector<std::string> subStrings;
  std::size_t sIdx = 0;
  std::size_t eIdx = 0;
  while ((eIdx = inString.find(delimeter, sIdx)) < inString.size())
  {
    subStrings.push_back(inString.substr(sIdx, eIdx - sIdx));
    sIdx = eIdx + delimeter.size();
  }
  if (sIdx < inString.size())
  {
    subStrings.push_back(inString.substr(sIdx));
  }
  return subStrings;
}

vtkPartitionedDataSet* GetNamedPartitionedDataSet(
  const std::string& name, vtkPartitionedDataSetCollection* input)
{
  vtkDataAssembly* assembly = input->GetDataAssembly();
  const std::string selector = "//" + vtkDataAssembly::MakeValidNodeName(name.c_str());
  std::vector<int> nodeIds = assembly->SelectNodes({ selector });

  if (nodeIds.empty())
  {
    return nullptr;
  }

  const auto ids = assembly->GetDataSetIndices(nodeIds[0]);
  if (ids.empty())
  {
    return nullptr;
  }
  return input->GetPartitionedDataSet(ids[0]);
}

std::vector<double> GetEdgeAttributes(
  const std::string& name, vtkCellData* cd, const vtkIdType& cellId)
{
  std::vector<double> attrs;
  vtkDataArray* coeffs = cd->GetArray(::GetEdgeCoeffArrName(name).c_str());
  if (coeffs == nullptr)
  {
    return attrs;
  }
  const int& nEdges = coeffs->GetNumberOfComponents();
  attrs.resize(nEdges);
  coeffs->GetTuple(cellId, attrs.data());
  return attrs;
}

std::vector<double> GetFaceAttributes(
  const std::string& name, vtkCellData* cd, const vtkIdType& cellId)
{
  std::vector<double> attrs;
  vtkDataArray* coeffs = cd->GetArray(::GetFaceCoeffArrName(name).c_str());
  if (coeffs == nullptr)
  {
    return attrs;
  }
  const int& nFaces = coeffs->GetNumberOfComponents();
  attrs.resize(nFaces);
  coeffs->GetTuple(cellId, attrs.data());
  return attrs;
}

using VblpMatrixType = vtkVectorBasisLagrangeProducts::VblpMatrixType;
using SpaceType = vtkVectorBasisLagrangeProducts::SpaceType;

void InterpolateToNodes(const VblpMatrixType& vblpmat, const std::vector<double>& coeffs,
  const vtkIdType& npts, const vtkIdType* pts, vtkDataArray* result)
{
  const std::size_t& nDofs = coeffs.size();
  assert(vblpmat.size() == 3);
  assert(vblpmat[0].size() == npts);
  assert(vblpmat[1].size() == npts);
  assert(vblpmat[2].size() == npts);

  for (vtkIdType j = 0; j < npts; ++j)
  {
    const vtkIdType& ptId = pts[j];
    double value[3] = { 0, 0, 0 };

    // interpolate field from edge -> nodal dof
    for (std::size_t k = 0; k < vblpmat.size(); ++k)
    {
      assert(vblpmat[k][j].size() == nDofs);
      for (std::size_t i = 0; i < nDofs; ++i)
      {
        value[k] += vblpmat[k][j][i] * coeffs[i];
      } // for i'th edge.
    }   // for every component of vector basis function.
    // save new values.
    result->InsertTuple(ptId, value);
  }
}

std::vector<int> GetIOSSTransformation(const VTKCellType& cellType, const int& npts)
{
  std::vector<int> result;
  switch (cellType)
  {
    case VTK_LINE:
    case VTK_LAGRANGE_CURVE:
      switch (npts)
      {
        case 2:
        case 3:
        case 4:
          result.resize(npts, 0);
          std::iota(result.begin(), result.end(), 1);
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of points for cell - VTK_LINE."
                          << "Supported: One of 2, 3, 4 "
                          << "Got: " << npts);
          break;
      }
      break;
    case VTK_TRIANGLE:
    case VTK_LAGRANGE_TRIANGLE:
      switch (npts)
      {
        case 3:
        case 6:
        case 10:
          result.resize(npts, 0);
          std::iota(result.begin(), result.end(), 1);
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of points for cell - VTK_TRIANGLE."
                          << "Supported: One of 3, 6, 10"
                          << "Got: " << npts);
          break;
      }
      break;
    case VTK_QUAD:
    case VTK_LAGRANGE_QUADRILATERAL:
      switch (npts)
      {
        case 4:
        case 9:
        case 16:
          result.resize(npts, 0);
          std::iota(result.begin(), result.end(), 1);
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of points for cell - VTK_QUAD."
                          << "Supported: One of 4, 9, 16 "
                          << "Got: " << npts);
          break;
      }
      break;
    case VTK_TETRA:
    case VTK_LAGRANGE_TETRAHEDRON:
      switch (npts)
      {
        case 4:
        case 10:
        case 11:
        case 15:
          result.resize(npts, 0);
          std::iota(result.begin(), result.end(), 1);
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of points for cell - VTK_TETRA."
                          << "Supported: One of 4, 10, 11, 15 "
                          << "Got: " << npts);
          break;
      }
      break;
    case VTK_PYRAMID:
    case VTK_LAGRANGE_PYRAMID:
      // vtk does not have vtkHigherOrderPyramid.
      switch (npts)
      {
        case 5:
        case 13:
        case 14:
        case 19:
        default:
          vtkLog(WARNING, << "Unsupported no. of points for cell - VTK_PYRAMID."
                          << "Supported: None "
                          << "Got: " << npts);
          break;
      }
      break;
    case VTK_WEDGE:
    case VTK_LAGRANGE_WEDGE:
      switch (npts)
      {
        case 6:
          result = { 4, 5, 6, 1, 2, 3 };
          break;
        case 15:
          // clang-format off
          result = {
            4, 5, 6, 1, 2, 3,
            13, 14, 15,
            7, 8, 9,
            10, 11, 12
          };
          // clang-format on
          break;
        case 18:
          // clang-format off
          result = {
            /* 2 triangles */
            4, 5, 6, 1, 2, 3,

            /* edge centers */
            13, 14, 15,
            7, 8, 9,
            10, 11, 12,

            /* quad-centers */
            16, 17, 18
          };
          // clang-format on
          break;
        case 21:
          result.resize(npts, 0);
          std::iota(result.begin(), result.end(), 1);
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of points for cell - VTK_WEDGE."
                          << "Supported: 15, 18, 21 "
                          << "Got: " << npts);
          break;
      }
      break;
    case VTK_HEXAHEDRON:
    case VTK_LAGRANGE_HEXAHEDRON:
      switch (npts)
      {
        case 8:
          result.resize(npts, 0);
          std::iota(result.begin(), result.end(), 1);
          break;
        case 20:
          // clang-format off
          result = {
            /* 8 corners */
            1, 2, 3, 4,
            5, 6, 7, 8,

            /* 12 mid-edge nodes */
            9, 10, 11, 12,
            17, 18, 19, 20,
            13, 14, 15, 16
          };
          // clang-format on
          break;
        case 27:
          // clang-format off
          result = {
            /* 8 corners */
            1, 2, 3, 4,
            5, 6, 7, 8,

            /* 12 mid-edge nodes */
            9, 10, 11, 12,
            17, 18, 19, 20,
            13, 14, 15, 16,

            /* 6 mid-face nodes */
            24, 25, 26, 27, 22, 23,

            /* mid-volume node*/
            21
          };
          // clang-format on
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of points for cell - VTK_HEXAHEDRON."
                          << "Supported: 8, 20, 27 "
                          << "Got: " << npts);
          break;
      }
      break;
    default:
      break;
  }
  return result;
}
}

class vtkFiniteElementFieldDistributor::vtkInternals
{
public:
  // clang-format off
  std::unordered_map<std::string, ::vtkFiniteElementSpec> femSpecs {
    { "HCURL", ::vtkFiniteElementSpec() },
    { "HDIV", ::vtkFiniteElementSpec() },
    { "HGRAD", ::vtkFiniteElementSpec() }
  };
  // clang-format on
  void InitializeReferenceElement(const int& order);

  void Allocate(vtkPoints* newPoints, vtkCellArray* newCells, vtkUnsignedCharArray* newCellTypes,
    vtkPointData* hGradFields, vtkPointData* hCurlFields, vtkPointData* hDivFields,
    vtkUnstructuredGrid* elements);

  //  takes a continuous mesh and explodes the point set such that each element has
  // its own collection of points unshared by any other element. This also
  // converts the mesh into potentially a higher order mesh if the DG fields require it
  void ExplodeCell(const vtkIdType& cellId, vtkPoints* oldPoints, vtkPoints* newPoints,
    vtkCellArray* oldCells, vtkCellArray* newCells, vtkUnsignedCharArray* newCellTypes,
    vtkPointData* oldPd, vtkPointData* newPd, vtkCellData* oldCd, vtkPointData* hGradFields);

  // Interpolates edge -> nodal dofs.
  // Interpolates face -> nodal dofs.
  void InterpolateCellToNodes(const vtkIdType& cellId, vtkCellArray* oldCells,
    vtkCellArray* newCells, vtkCellData* oldCd, vtkPointData* hCurlFields,
    vtkPointData* hDivFields);

  // clear the three slots of femSpecs.
  void ResetFemSpecs();

private:
  // for convenient access to spec
  inline ::vtkFiniteElementSpec& hCurlSpec() { return this->femSpecs["HCURL"]; }
  inline ::vtkFiniteElementSpec& hDivSpec() { return this->femSpecs["HDIV"]; }
  inline ::vtkFiniteElementSpec& hGradSpec() { return this->femSpecs["HGRAD"]; }

  void AllocateGeometry(vtkPoints* newPoints, const vtkIdType& maxCellSize, vtkCellArray* newCells,
    vtkUnsignedCharArray* newCellTypes, const vtkIdType& numCells);

  void AllocateFields(vtkPointData* hGradFields, vtkPointData* hCurlFields,
    vtkPointData* hDivFields, vtkUnstructuredGrid* elements, const vtkIdType& maxNumPoints);

  static void ExplodeDGHGradCellCenteredField(vtkCellData* inCd, vtkPointData* outPd,
    const char* name, const vtkIdType& cellId, const vtkIdType& npts, const vtkIdType* pts,
    const std::vector<int>& orderingTransform);

  void ExplodeLinearCell(const vtkIdType& cellId, vtkPoints* oldPoints, vtkPoints* newPoints,
    vtkCellArray* oldCells, vtkCellArray* newCells, vtkUnsignedCharArray* newCellTypes,
    vtkPointData* oldPd, vtkPointData* newPd);

  void ExplodeHigherOrderCell(const vtkIdType& cellId, vtkPoints* oldPoints, vtkPoints* newPoints,
    vtkCellArray* oldCells, vtkCellArray* newCells, vtkUnsignedCharArray* newCellTypes,
    vtkPointData* oldPd, vtkPointData* newPd, vtkCellData* oldCd, const int& nComps);

  std::vector<double> GetLagrangePCoords(const VTKCellType& cellType, const vtkIdType& npts);

  vtkVectorBasisLagrangeProducts Vblps;
  VTKCellType RefElement = VTK_EMPTY_CELL;
  int Order = 0;
  vtkNew<vtkDoubleArray> weights; // resized to maxCellSize in AllocateGeometry. Use it as you wish.
  // typed vtkCell instances allows easy access to parametric coordinates, edges, faces, ...
  vtkNew<vtkHexahedron> hex;
  vtkNew<vtkLine> line;
  vtkNew<vtkQuad> quad;
  vtkNew<vtkTriangle> tri;
  vtkNew<vtkTetra> tet;
  vtkNew<vtkWedge> wedge;
  vtkNew<vtkLagrangeHexahedron> lagHex;
  vtkNew<vtkLagrangeCurve> lagCurve;
  vtkNew<vtkLagrangeQuadrilateral> lagQuad;
  vtkNew<vtkLagrangeTriangle> lagTri;
  vtkNew<vtkLagrangeTetra> lagTet;
  vtkNew<vtkLagrangeWedge> lagWedge;
};

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::InitializeReferenceElement(const int& order)
{
  std::unordered_set<VTKCellType> cellTypes;
  cellTypes.insert(this->hCurlSpec().RefElement);
  cellTypes.insert(this->hDivSpec().RefElement);
  cellTypes.insert(this->hGradSpec().RefElement);
  cellTypes.erase(VTK_EMPTY_CELL);
  this->RefElement = cellTypes.size() == 1 ? *(cellTypes.begin()) : VTK_EMPTY_CELL;
  this->Order = order;
}

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::ResetFemSpecs()
{
  for (auto& femSpec : this->femSpecs)
  {
    femSpec.second = ::vtkFiniteElementSpec();
  }
  this->RefElement = VTK_EMPTY_CELL;
  this->Order = 0;
}

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::AllocateGeometry(vtkPoints* newPoints,
  const vtkIdType& maxCellSize, vtkCellArray* newCells, vtkUnsignedCharArray* newCellTypes,
  const vtkIdType& numCells)
{
  const vtkIdType maxNumPoints = numCells * maxCellSize;
  newCellTypes->SetNumberOfComponents(1);
  newCellTypes->SetNumberOfValues(numCells);
  newCells->AllocateEstimate(numCells, maxCellSize);
  newPoints->Allocate(maxNumPoints);
  this->weights->SetNumberOfValues(maxCellSize);
}

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::AllocateFields(vtkPointData* hGradFields,
  vtkPointData* hCurlFields, vtkPointData* hDivFields, vtkUnstructuredGrid* elements,
  const vtkIdType& maxNumPoints)
{
  vtkCellData* elemCd = elements->GetCellData();

  // Prepare HGRAD fields
  for (const auto& field : this->hGradSpec().Fields)
  {
    const char* name = field.c_str();
    vtkDataArray* inArr = elemCd->GetArray(name);
    if (inArr == nullptr)
    {
      continue;
    }
    auto arr = vtk::TakeSmartPointer(::InitializeNewArray(inArr, name, 1, 0));
    arr->Allocate(maxNumPoints);
    hGradFields->AddArray(arr);
  }
  // The new nodal form of HCurl fields will go into point data.
  for (const auto& fieldName : this->hCurlSpec().Fields)
  {
    const std::string& name = ::GetEdgeCoeffArrName(fieldName);
    vtkDataArray* inArr = elemCd->GetArray(name.c_str());
    auto arr = vtk::TakeSmartPointer(::InitializeNewArray(inArr, fieldName, 3, 0));
    arr->Allocate(maxNumPoints);
    hCurlFields->AddArray(arr);
  }
  // The new nodal form of HDiv fields will go into point data.
  for (const auto& fieldName : this->hDivSpec().Fields)
  {
    const std::string& name = ::GetFaceCoeffArrName(fieldName);
    vtkDataArray* inArr = elemCd->GetArray(name.c_str());
    auto arr = vtk::TakeSmartPointer(::InitializeNewArray(inArr, fieldName, 3, 0));
    arr->Allocate(maxNumPoints);
    hDivFields->AddArray(arr);
  }
}

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::Allocate(vtkPoints* newPoints,
  vtkCellArray* newCells, vtkUnsignedCharArray* newCellTypes, vtkPointData* hGradFields,
  vtkPointData* hCurlFields, vtkPointData* hDivFields, vtkUnstructuredGrid* elements)
{
  if (elements == nullptr)
  {
    return;
  }
  if (elements->GetCells() == nullptr)
  {
    return;
  }

  const vtkIdType& nCells = elements->GetNumberOfCells();
  const vtkIdType& maxCellSize = elements->GetCells()->GetMaxCellSize();
  const vtkIdType maxNpts = nCells * maxCellSize;
  this->AllocateGeometry(newPoints, maxCellSize, newCells, newCellTypes, nCells);
  this->AllocateFields(hGradFields, hCurlFields, hDivFields, elements, maxNpts);
}

//----------------------------------------------------------------------------
std::vector<double> vtkFiniteElementFieldDistributor::vtkInternals::GetLagrangePCoords(
  const VTKCellType& cellType, const vtkIdType& npts)
{
  vtkCell* cell = nullptr;
  switch (cellType)
  {
    case VTK_HEXAHEDRON:
      this->lagHex->SetUniformOrderFromNumPoints(npts);
      cell = this->lagHex;
      break;
    case VTK_QUAD:
      this->lagQuad->SetUniformOrderFromNumPoints(npts);
      cell = this->lagQuad;
      break;
    case VTK_TETRA:
      cell = this->lagTet;
      break;
    case VTK_TRIANGLE:
      cell = this->lagTri;
      break;
    case VTK_WEDGE:
      cell = this->lagWedge;
      break;
    default:
      break;
  }
  if (cell != nullptr)
  {
    cell->PointIds->SetNumberOfIds(npts);
    cell->Points->SetNumberOfPoints(npts);
    cell->Initialize();
    double* pCoords = cell->GetParametricCoords();
    return std::vector<double>(pCoords, pCoords + npts * 3);
  }
  else
  {
    return {};
  }
}

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::ExplodeCell(const vtkIdType& cellId,
  vtkPoints* oldPoints, vtkPoints* newPoints, vtkCellArray* oldCells, vtkCellArray* newCells,
  vtkUnsignedCharArray* newCellTypes, vtkPointData* oldPd, vtkPointData* newPd, vtkCellData* oldCd,
  vtkPointData* hGradFields)
{
  // loop over cell connectivity, redo the connectivity s.t each cell is
  // disconnected from other cells and then copy associated points into
  // the point array.
  if (this->Order == 1)
  {
    this->ExplodeLinearCell(
      cellId, oldPoints, newPoints, oldCells, newCells, newCellTypes, oldPd, newPd);
  }
  else
  {
    // Determine the order from no. of components in HGrad DG field arrays.
    std::unordered_set<int> nCompsSet;
    for (const auto& field : this->hGradSpec().Fields)
    {
      const char* name = field.c_str();
      vtkDataArray* arr = oldCd->GetArray(name);
      if (arr != nullptr)
      {
        const int& nComps = arr->GetNumberOfComponents();
        nCompsSet.insert(nComps);
      }
    }
    if (nCompsSet.size() != 1)
    {
      vtkLog(WARNING,
        << "Invalid no. of components for HGrad DG fields. Unable to determine order of cell "
        << cellId);
      return;
    }

    int nComps = *(nCompsSet.begin());
    this->ExplodeHigherOrderCell(
      cellId, oldPoints, newPoints, oldCells, newCells, newCellTypes, oldPd, newPd, oldCd, nComps);
  }

  // explode n-component cell centered HGrad DG (Discontinuous Galerkin) field from cell -> nodes.
  vtkIdType newNpts = 0;
  const vtkIdType* newPts = nullptr;
  newCells->GetCellAtId(cellId, newNpts, newPts);
  // the field components follow ioss element ordering.
  auto ordering = ::GetIOSSTransformation(this->RefElement, newNpts);
  // ioss elements are 1-indexed. transform to 0-indexed lists.
  std::transform(ordering.cbegin(), ordering.cend(), ordering.begin(),
    [](const vtkIdType& val) { return val - 1; });
  // explode HGrad dg fields with the transformation.
  for (const auto& field : this->hGradSpec().Fields)
  {
    const char* name = field.c_str();
    this->ExplodeDGHGradCellCenteredField(
      oldCd, hGradFields, name, cellId, newNpts, newPts, ordering);
  }
}

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::ExplodeLinearCell(const vtkIdType& cellId,
  vtkPoints* oldPoints, vtkPoints* newPoints, vtkCellArray* oldCells, vtkCellArray* newCells,
  vtkUnsignedCharArray* newCellTypes, vtkPointData* oldPd, vtkPointData* newPd)
{
  vtkIdType newNpts = 0, oldNpts = 0, ind = newPoints->GetNumberOfPoints();
  const vtkIdType* oldPts = nullptr;
  double coord[3] = {};

  oldCells->GetCellAtId(cellId, oldNpts, oldPts);
  newCellTypes->SetValue(cellId, this->RefElement);
  newCells->InsertNextCell(oldNpts);

  for (vtkIdType i = 0; i < oldNpts; ++i, ++ind)
  {
    const auto& oldId = oldPts[i];
    oldPoints->GetPoint(oldId, coord);
    newPoints->InsertPoint(ind, coord);
    newCells->InsertCellPoint(ind);
    // copy over the non-dg fields from old -> new point data
    newPd->CopyData(oldPd, oldId, ind);
  }
}

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::ExplodeHigherOrderCell(const vtkIdType& cellId,
  vtkPoints* oldPoints, vtkPoints* newPoints, vtkCellArray* oldCells, vtkCellArray* newCells,
  vtkUnsignedCharArray* newCellTypes, vtkPointData* oldPd, vtkPointData* newPd, vtkCellData* oldCd,
  const int& nComps)
{
  vtkNonLinearCell* nonLinCell = nullptr;
  vtkCell* linearCell = nullptr;
  vtkNew<vtkIdList> oldPtIds;
  oldCells->GetCellAtId(cellId, oldPtIds);

  const vtkIdType& oldNpts = oldPtIds->GetNumberOfIds();
  vtkIdType newNpts = 0;

  switch (this->RefElement)
  {
    case VTK_LINE:
      switch (nComps)
      {
        case 3:
        case 4:
          // bump to VTK_LAGRANGE_CURVE order 2
          newNpts = (oldNpts != nComps) ? nComps : oldNpts;
          nonLinCell = this->lagCurve;
          linearCell = this->line;
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of components in HGRAD field for cell - VTK_LINE."
                          << "Supported: One of 3, 4 "
                          << "Got: " << nComps);
          break;
      }
      break;
    case VTK_TRIANGLE:
      switch (nComps)
      {
        case 6:
        case 10:
          // bump to VTK_LAGRANGE_TRIANGLE order 2
          newNpts = (oldNpts != nComps) ? nComps : oldNpts;
          nonLinCell = this->lagTri;
          linearCell = this->tri;
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of components in HGRAD field for cell - VTK_TRIANGLE."
                          << "Supported: One of 6, 10"
                          << "Got: " << nComps);
          break;
      }
      break;
    case VTK_QUAD:
      switch (nComps)
      {
        case 9:
        case 16:
          // bump to VTK_LAGRANGE_QUADRILATERAL order n
          newNpts = (oldNpts != nComps) ? nComps : oldNpts;
          this->lagQuad->SetUniformOrderFromNumPoints(newNpts);
          nonLinCell = this->lagQuad;
          linearCell = this->quad;
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of components in HGRAD field for cell - VTK_QUAD."
                          << "Supported: One of 9, 16 "
                          << "Got: " << nComps);
          break;
      }
      break;
    case VTK_TETRA:
      switch (nComps)
      {
        case 10:
        case 11:
        case 15:
          // bump to VTK_LAGRANGE_TETRAHEDRON order n
          newNpts = (oldNpts != nComps) ? nComps : oldNpts;
          nonLinCell = this->lagTet;
          linearCell = this->tet;
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of components in HGRAD field for cell - VTK_TETRA."
                          << "Supported: One of 10, 11, 15 "
                          << "Got: " << nComps);
          break;
      }
      break;
    case VTK_PYRAMID:
      // vtk does not have vtkHigherOrderPyramid.
      switch (nComps)
      {
        case 13:
        case 14:
        case 19:
        default:
          vtkLog(WARNING, << "Unsupported no. of components in HGRAD field for cell - VTK_PYRAMID."
                          << "Supported: None "
                          << "Got: " << nComps);
          break;
      }
      break;
    case VTK_WEDGE:
      switch (nComps)
      {
        case 15:
        case 18:
        case 21:
          // bump to VTK_LAGRANGE_WEDGE order n
          newNpts = (oldNpts != nComps) ? nComps : oldNpts;
          this->lagWedge->SetUniformOrderFromNumPoints(newNpts);
          nonLinCell = this->lagWedge;
          linearCell = this->wedge;
          break;
        default:
          vtkLog(WARNING, << "Unsupported no. of components in HGRAD field for cell - VTK_WEDGE."
                          << "Supported: 15, 18, 21 "
                          << "Got: " << nComps);
          break;
      }
      break;
    case VTK_HEXAHEDRON:
      switch (nComps)
      {
        case 20:
        case 27:
          // bump to VTK_LAGRANGE_HEXAHEDRON order n
          newNpts = (oldNpts != nComps) ? nComps : oldNpts;
          this->lagHex->SetUniformOrderFromNumPoints(newNpts);
          nonLinCell = this->lagHex;
          linearCell = this->hex;
          break;
        default:
          vtkLog(
            WARNING, << "Unsupported no. of components in HGRAD field for cell - VTK_HEXAHEDRON."
                     << "Supported: 20, 27 "
                     << "Got: " << nComps);
          break;
      }
      break;
    default:
      vtkLog(WARNING, << "Unsupported higher order cell: " << this->RefElement);
      break;
  }

  if (nonLinCell != nullptr)
  {
    double* pCoords = nullptr;
    double coord[3] = {};
    int subId = 0;
    const vtkIdType* oldPts = oldPtIds->GetPointer(0);
    vtkIdType ind = newPoints->GetNumberOfPoints();

    newCells->InsertNextCell(newNpts);
    newCellTypes->SetValue(cellId, nonLinCell->GetCellType());

    // insert points from old cell.
    for (unsigned short i = 0; i < oldNpts; ++i, ++ind)
    {
      const auto& oldId = oldPts[i];
      oldPoints->GetPoint(oldId, coord);
      newPoints->InsertPoint(ind, coord);
      newCells->InsertCellPoint(ind);
      // copy over the non-dg fields from old -> new point data
      newPd->CopyData(oldPd, oldId, ind);
    }

    // need to construct a higher order cell from a linear cell.
    if (linearCell != nullptr)
    {
      linearCell->Initialize(oldNpts, oldPts, oldPoints);
      // add points at mid-edge, mid-face locations or at volume center.
      nonLinCell->Points->SetNumberOfPoints(newNpts);
      nonLinCell->PointIds->SetNumberOfIds(newNpts);
      nonLinCell->Initialize();
      if (this->weights->GetNumberOfValues() < oldNpts)
      {
        // resize to adjust for bigger cells as needed.
        this->weights->SetNumberOfValues(oldNpts);
      }
      this->weights->FillValue(0.0);
      pCoords = nonLinCell->GetParametricCoords();
      for (unsigned short i = oldNpts; i < newNpts; ++i, ++ind)
      {
        linearCell->EvaluateLocation(subId, &pCoords[3 * i], coord, this->weights->GetPointer(0));
        newPoints->InsertPoint(ind, coord);
        newCells->InsertCellPoint(ind);
        // interpolate the non-dg fields from old -> new point data
        newPd->InterpolatePoint(oldPd, ind, oldPtIds, this->weights->GetPointer(0));
      }
    }
  }
}

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::ExplodeDGHGradCellCenteredField(
  vtkCellData* inCd, vtkPointData* outPd, const char* name, const vtkIdType& cellId,
  const vtkIdType& npts, const vtkIdType* pts, const std::vector<int>& orderingTransform)
{
  vtkDataArray* const inArr = inCd->GetArray(name);
  vtkDataArray* const outArr = outPd->GetArray(name);
  if (inArr == nullptr || outArr == nullptr)
  {
    vtkLog(WARNING, << "Invalid HGRAD DG field data. Cannot find array : " << name);
    return;
  }

  if (inArr->GetNumberOfComponents() == npts)
  {
    if (orderingTransform.size() == npts)
    {
      for (vtkIdType i = 0; i < npts; ++i)
      {
        double value =
          inArr->GetComponent(cellId, orderingTransform[i]); // get the transformed i'th component
        outArr->InsertComponent(pts[i], 0, value);
      }
    }
    else
    { // fallback to naïve ordering
      for (vtkIdType i = 0; i < npts; ++i)
      {
        double value = inArr->GetComponent(cellId, i);
        outArr->InsertComponent(pts[i], 0, value);
      }
    }
  }
  else
  {
    vtkLog(WARNING, << "HGRAD field(" << name << ") component mismatch. CellSize(" << npts
                    << ") != nComps(" << inArr->GetNumberOfComponents() << ")");
  }
}

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::vtkInternals::InterpolateCellToNodes(const vtkIdType& cellId,
  vtkCellArray* oldCells, vtkCellArray* newCells, vtkCellData* oldCd, vtkPointData* hCurlFields,
  vtkPointData* hDivFields)
{
  // we will interpolate onto the points found at new point ids. (from cell explosion)
  const vtkIdType* newPts = nullptr;
  vtkIdType newNpts = 0;
  newCells->GetCellAtId(cellId, newNpts, newPts);
  if (this->Vblps.RequiresInitialization(this->RefElement, nullptr, newNpts))
  {
    auto pCoords = this->GetLagrangePCoords(this->RefElement, newNpts);
    // for all others, need to shift center of element to (0,0,0)
    if (this->RefElement != VTK_TRIANGLE && this->RefElement != VTK_TETRA)
    {
      std::transform(pCoords.cbegin(), pCoords.cend(), pCoords.begin(),
        [](const double& val) -> double { return 2 * (val - 0.5); });
    }
    this->Vblps.Initialize(this->RefElement, pCoords.data(), newNpts);
  }

  for (const auto& fieldName : this->hCurlSpec().Fields)
  {
    std::vector<double> coeffs = ::GetEdgeAttributes(fieldName, oldCd, cellId);
    if (coeffs.empty())
    {
      continue;
    }
    vtkDataArray* outArr = hCurlFields->GetArray(fieldName.c_str());
    const auto vblpmat = this->Vblps.GetVblp(::SpaceType::HCurl, this->RefElement);
    if (vblpmat != nullptr)
    {
      ::InterpolateToNodes(*vblpmat, coeffs, newNpts, newPts, outArr);
    }
  }

  for (const auto& fieldName : this->hDivSpec().Fields)
  {
    std::vector<double> coeffs;
    if (this->RefElement == VTK_QUAD || this->RefElement == VTK_TRIANGLE)
    {
      coeffs = std::move(::GetEdgeAttributes(fieldName, oldCd, cellId));
    }
    else
    {
      coeffs = std::move(::GetFaceAttributes(fieldName, oldCd, cellId));
    }
    if (coeffs.empty())
    {
      continue;
    }
    vtkDataArray* outArr = hDivFields->GetArray(fieldName.c_str());
    const auto vblpmat = this->Vblps.GetVblp(::SpaceType::HDiv, this->RefElement);
    if (vblpmat != nullptr)
    {
      ::InterpolateToNodes(*vblpmat, coeffs, newNpts, newPts, outArr);
    }
  }
}

vtkStandardNewMacro(vtkFiniteElementFieldDistributor);

//----------------------------------------------------------------------------
vtkFiniteElementFieldDistributor::vtkFiniteElementFieldDistributor()
  : Internals(new vtkFiniteElementFieldDistributor::vtkInternals())
{
}

//----------------------------------------------------------------------------
vtkFiniteElementFieldDistributor::~vtkFiniteElementFieldDistributor() = default;

//----------------------------------------------------------------------------
void vtkFiniteElementFieldDistributor::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
int vtkFiniteElementFieldDistributor::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  this->Internals->ResetFemSpecs();

  vtkPartitionedDataSetCollection* input = vtkPartitionedDataSetCollection::GetData(inputVector[0]);
  vtkPartitionedDataSetCollection* output = vtkPartitionedDataSetCollection::GetData(outputVector);

  // Look for special string array containing information records.
  vtkFieldData* fd = input->GetFieldData();
  vtkStringArray* infoRecords =
    vtkStringArray::SafeDownCast(fd->GetAbstractArray("Information Records"));
  if (infoRecords == nullptr)
  {
    vtkErrorMacro(<< "Failed to find a string array 'Information Records'");
    return 0;
  }

  // Parse the information records.
  int refElementOrder = 0;
  std::unordered_set<std::string> elementBlockNames;
  for (vtkIdType i = 0; i < infoRecords->GetNumberOfValues(); ++i)
  {
    const auto& record = infoRecords->GetValue(i);
    ::vtkFiniteElementSpec* femSpec = nullptr;

    const std::vector<std::string> data = ::Split(record, "::");
    // Examples:
    // "HDIV::eblock-0_0_0::CG::basis::Intrepid2_HDIV_HEX_I1_FEM"
    //    0       1         2     3              4
    //
    // "HGRAD::eblock-0_0::DG::basis::Intrepid2_HGRAD_QUAD_C2_FEM"
    //    0       1        2     3              4
    //
    // "HCURL::eblock-0_0_0::CG::basis::Intrepid2_HCURL_HEX_I1_FEM"
    //    0       1          2     3              4
    //
    // "HCURL::eblock-0_0_0::CG::field::E_Field"
    //    0       1          2     3      4
    if (data.size() < 5)
    {
      continue;
    }
    // within this context, an entity is either a basis or a field.
    const std::string& basisType = data[0];
    const std::string& blockName = data[1];
    const std::string& galerkinType = data[2];
    const std::string& entityType = data[3];
    const std::string& entityName = data[4];
    // Look for valid FEM element callouts.
    if (!(basisType == "HCURL" || basisType == "HDIV" || basisType == "HGRAD"))
    {
      continue;
    }
    if (basisType == "HGRAD")
    { // only element block has a HGRAD basis definition
      elementBlockNames.insert(blockName);
    }

    femSpec = &(this->Internals->femSpecs[basisType]);

    if (entityType == "basis")
    {
      const auto& intrepidName = entityName;
      const std::vector<std::string> nameParts = ::Split(intrepidName, "_");
      // Examples:
      // "Intrepid2_HCURL_HEX_I1_FEM"
      //      0       1    2  3   4
      const int currentBasisOrder = nameParts[3][1] - '0';
      if (galerkinType == "CG")
      {
        refElementOrder = currentBasisOrder > refElementOrder ? currentBasisOrder : refElementOrder;
      }
      else if (galerkinType == "DG")
      {
        refElementOrder = currentBasisOrder > refElementOrder ? currentBasisOrder : refElementOrder;
      }
      const auto& elementName = nameParts[2];
      if (elementName == "HEX")
      {
        femSpec->RefElement = VTK_HEXAHEDRON;
      }
      else if (elementName == "LINE")
      {
        femSpec->RefElement = VTK_LINE;
      }
      else if (elementName == "PYR")
      {
        femSpec->RefElement = VTK_PYRAMID;
      }
      else if (elementName == "QUAD")
      {
        femSpec->RefElement = VTK_QUAD;
      }
      else if (elementName == "TET")
      {
        femSpec->RefElement = VTK_TETRA;
      }
      else if (elementName == "TRI")
      {
        femSpec->RefElement = VTK_TRIANGLE;
      }
      else if (elementName == "WEDGE")
      {
        femSpec->RefElement = VTK_WEDGE;
      }
    }
    else if (entityType == "field" && femSpec != nullptr)
    {
      // these fields will be attached to a basis.
      if (galerkinType == "CG" && basisType != "HGRAD")
      {
        femSpec->Fields.insert(entityName);
      }
      else if (galerkinType == "DG" && basisType == "HGRAD")
      {
        femSpec->Fields.insert(entityName);
      }
    }
  }
  if (elementBlockNames.empty())
  {
    vtkErrorMacro(<< "Failed to find element blocks!");
    return 0;
  }

  this->Internals->InitializeReferenceElement(refElementOrder);

  bool abortNow = false;
  unsigned int pdsIdx = 0;
  for (const auto& blockName : elementBlockNames)
  {
    if (abortNow)
    {
      break;
    }
    vtkPartitionedDataSet* elementsPds = nullptr;
    // Find an element block.
    if (!blockName.empty())
    {
      elementsPds = ::GetNamedPartitionedDataSet(blockName, input);
    }
    if (elementsPds == nullptr)
    {
      continue;
    }

    // TODO: mpi-fy this thing..
    const unsigned int numParts = elementsPds->GetNumberOfPartitions();
    for (unsigned int partIdx = 0; partIdx < numParts && !abortNow; ++partIdx)
    {
      vtkUnstructuredGrid* elements =
        vtkUnstructuredGrid::SafeDownCast(elementsPds->GetPartition(partIdx));
      if (elements == nullptr || elements->GetNumberOfPoints() == 0 ||
        elements->GetNumberOfCells() == 0)
      {
        continue;
      }

      vtkPoints* oldPoints = elements->GetPoints();
      vtkCellArray* oldCells = elements->GetCells();

      // peek at the elements block to allocate appropriate output.
      vtkNew<vtkUnstructuredGrid> newMesh;
      vtkNew<vtkUnsignedCharArray> newCellTypes;
      vtkNew<vtkPointData> hGradFields, hCurlFields, hDivFields;
      auto newPoints = vtk::TakeSmartPointer(oldPoints->NewInstance());
      auto newCells = vtk::TakeSmartPointer(oldCells->NewInstance());
      this->Internals->Allocate(
        newPoints, newCells, newCellTypes, hGradFields, hCurlFields, hDivFields, elements);

      // copy/interpolate dataset attributes.
      vtkCellData *oldCd = elements->GetCellData(), *newCd = newMesh->GetCellData();
      vtkPointData *oldPd = elements->GetPointData(), *newPd = newMesh->GetPointData();
      vtkFieldData *oldFd = elements->GetFieldData(), *newFd = newMesh->GetFieldData();
      // when we bump cell order, new points are created. requires weighted interpolation for
      // CG (Continuous Galerkin) point data arrays.
      newPd->InterpolateAllocate(oldPd);
      newCd->CopyAllocate(oldCd);
      newFd->DeepCopy(oldFd);

      // explode geometry, interpolate fields.
      const double progressGranularity = 0.1;
      const vtkIdType& nCells = oldCells->GetNumberOfCells();
      const vtkIdType reportEveryNCells = progressGranularity * nCells;
      for (vtkIdType c = 0; c < nCells && !abortNow; ++c)
      {
        this->Internals->ExplodeCell(c, oldPoints, newPoints, oldCells, newCells, newCellTypes,
          oldPd, newPd, oldCd, hGradFields);
        this->Internals->InterpolateCellToNodes(
          c, oldCells, newCells, oldCd, hCurlFields, hDivFields);

        newCd->CopyData(oldCd, c, c);

        if (c % reportEveryNCells == 0)
        {
          abortNow = this->GetAbortExecute();
          this->UpdateProgress(static_cast<double>(c) / nCells);
        }
      } // for each cell
      if (abortNow)
      {
        continue;
      }

      // Finalize geometry, topology of output mesh.
      newMesh->SetPoints(newPoints);
      newMesh->SetCells(newCellTypes, newCells);
      output->SetPartition(pdsIdx, partIdx, newMesh);
      output->GetMetaData(pdsIdx)->Set(vtkCompositeDataSet::NAME(), blockName.c_str());

      // Copy over the hgrad/hcurl/hdiv fields into output point data.
      for (int i = 0; i < hGradFields->GetNumberOfArrays(); ++i)
      {
        if (vtkDataArray* arr = hGradFields->GetArray(i))
        {
          if (arr->GetNumberOfTuples())
          {
            const char* name = hGradFields->GetArrayName(i);
            newPd->AddArray(arr);
            newCd->RemoveArray(name); // less clutter in the drop down menu in paraview.
          }
        }
      }
      for (int i = 0; i < hCurlFields->GetNumberOfArrays(); ++i)
      {
        if (vtkDataArray* arr = hCurlFields->GetArray(i))
        {
          if (arr->GetNumberOfTuples())
          {
            newPd->AddArray(arr);
          }
          // less clutter in the drop down menu in paraview.
          newCd->RemoveArray(::GetEdgeCoeffArrName(arr->GetName()).c_str());
        }
      }
      for (int i = 0; i < hDivFields->GetNumberOfArrays(); ++i)
      {
        if (vtkDataArray* arr = hDivFields->GetArray(i))
        {
          if (arr->GetNumberOfTuples())
          {
            newPd->AddArray(arr);
          }
          // less clutter in the drop down menu in paraview.
          newCd->RemoveArray(::GetFaceCoeffArrName(arr->GetName()).c_str());
        }
      }
    } // for each partition
    ++pdsIdx;
  } // for each element block
  return 1;
}
