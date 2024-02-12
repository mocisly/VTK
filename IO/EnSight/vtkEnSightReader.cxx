// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkEnSightReader.h"

#include "vtkDataArrayCollection.h"
#include "vtkDoubleArray.h"
#include "vtkFloatArray.h"
#include "vtkIdList.h"
#include "vtkIdListCollection.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMatrix4x4.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkObjectFactory.h"
#include "vtkRectilinearGrid.h"
#include "vtkSmartPointer.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkStructuredGrid.h"
#include "vtkStructuredPoints.h"
#include "vtkTransform.h"
#include "vtkTransformFilter.h"
#include "vtkUnstructuredGrid.h"
#include "vtksys/FStream.hxx"
#include "vtksys/SystemTools.hxx"

#include <algorithm>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
typedef std::vector<vtkSmartPointer<vtkIdList>> vtkEnSightReaderCellIdsTypeBase;
VTK_ABI_NAMESPACE_BEGIN
class vtkEnSightReaderCellIdsType : public vtkEnSightReaderCellIdsTypeBase
{
};

//------------------------------------------------------------------------------
vtkEnSightReader::vtkEnSightReader()
{
  this->MeasuredFileName = nullptr;
  this->MatchFileName = nullptr;
  this->RigidBodyFileName = nullptr;

  this->IS = nullptr;

  this->VariableMode = -1;

  this->UnstructuredPartIds = vtkIdList::New();
  this->CellIds = nullptr;

  this->VariableFileNames = nullptr;
  this->ComplexVariableFileNames = nullptr;

  this->VariableDescriptions = nullptr;
  this->ComplexVariableDescriptions = nullptr;

  this->VariableTimeSetIds = vtkIdList::New();
  this->ComplexVariableTimeSetIds = vtkIdList::New();
  this->VariableFileSetIds = vtkIdList::New();
  this->ComplexVariableFileSetIds = vtkIdList::New();

  this->TimeSetFileNameNumbers = vtkIdListCollection::New();
  this->TimeSetsWithFilenameNumbers = vtkIdList::New();
  this->TimeSets = vtkDataArrayCollection::New();
  this->FileSetFileNameNumbers = vtkIdListCollection::New();
  this->FileSetsWithFilenameNumbers = vtkIdList::New();
  this->FileSetNumberOfSteps = vtkIdListCollection::New();

  this->TimeSetIds = vtkIdList::New();
  this->FileSets = vtkIdList::New();

  this->GeometryTimeSet = 1;
  this->GeometryFileSet = 1;
  this->MeasuredTimeSet = 1;
  this->MeasuredFileSet = 1;

  this->UseTimeSets = 0;
  this->UseFileSets = 0;

  this->GeometryTimeValue = -1;
  this->MeasuredTimeValue = -1;

  this->NumberOfGeometryParts = 0;

  this->NumberOfMeasuredPoints = 0;

  this->InitialRead = 1;
  this->NumberOfNewOutputs = 0;

  this->UsePartNamesRB = true;
  this->UseEulerTimeSteps = false;
}

//------------------------------------------------------------------------------
vtkEnSightReader::~vtkEnSightReader()
{
  int i;

  delete this->CellIds;
  this->CellIds = nullptr;

  this->SetMeasuredFileName(nullptr);
  this->SetMatchFileName(nullptr);
  this->SetRigidBodyFileName(nullptr);

  if (this->NumberOfVariables > 0)
  {
    for (i = 0; i < this->NumberOfVariables; i++)
    {
      delete[] this->VariableFileNames[i];
    }
    delete[] this->VariableFileNames;
    this->VariableFileNames = nullptr;
  }

  if (this->NumberOfComplexVariables > 0)
  {
    for (i = 0; i < this->NumberOfComplexVariables * 2; i++)
    {
      delete[] this->ComplexVariableFileNames[i];
    }
    delete[] this->ComplexVariableFileNames;
    this->ComplexVariableFileNames = nullptr;
  }

  this->UnstructuredPartIds->Delete();
  this->UnstructuredPartIds = nullptr;

  this->VariableTimeSetIds->Delete();
  this->VariableTimeSetIds = nullptr;
  this->ComplexVariableTimeSetIds->Delete();
  this->ComplexVariableTimeSetIds = nullptr;
  this->VariableFileSetIds->Delete();
  this->VariableFileSetIds = nullptr;
  this->ComplexVariableFileSetIds->Delete();
  this->ComplexVariableFileSetIds = nullptr;

  this->TimeSetFileNameNumbers->Delete();
  this->TimeSetFileNameNumbers = nullptr;
  this->TimeSetsWithFilenameNumbers->Delete();
  this->TimeSetsWithFilenameNumbers = nullptr;
  this->TimeSets->Delete();
  this->TimeSets = nullptr;
  this->FileSetFileNameNumbers->Delete();
  this->FileSetFileNameNumbers = nullptr;
  this->FileSetsWithFilenameNumbers->Delete();
  this->FileSetsWithFilenameNumbers = nullptr;
  this->FileSetNumberOfSteps->Delete();
  this->FileSetNumberOfSteps = nullptr;

  this->TimeSetIds->Delete();
  this->TimeSets = nullptr;
  this->FileSets->Delete();
  this->FileSets = nullptr;

  this->ActualTimeValue = 0.0;
}

//------------------------------------------------------------------------------
void vtkEnSightReader::ClearForNewCaseFileName()
{
  this->UnstructuredPartIds->Reset();
  vtkGenericEnSightReader::ClearForNewCaseFileName();
}

//------------------------------------------------------------------------------
int vtkEnSightReader::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** vtkNotUsed(inputVector), vtkInformationVector* outputVector)
{
  vtkDebugMacro("In execute ");

  vtkInformation* outInfo = outputVector->GetInformationObject(0);
  vtkMultiBlockDataSet* output =
    vtkMultiBlockDataSet::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

  int tsLength = outInfo->Length(vtkStreamingDemandDrivenPipeline::TIME_STEPS());
  double* steps = outInfo->Get(vtkStreamingDemandDrivenPipeline::TIME_STEPS());

  this->ActualTimeValue = this->TimeValue;

  // Check if a particular time was requested by the pipeline.
  // This overrides the ivar.
  if (outInfo->Has(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP()) && tsLength > 0)
  {
    // Get the requested time step. We only support requests of a single time
    // step in this reader right now
    double requestedTimeStep = outInfo->Get(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP());

    // find the first time value larger than requested time value
    // this logic could be improved
    int cnt = 0;
    while (cnt < tsLength - 1 && steps[cnt] < requestedTimeStep)
    {
      cnt++;
    }
    this->ActualTimeValue = steps[cnt];
  }

  vtkDebugMacro("Executing with: " << this->ActualTimeValue);

  int i, timeSet, fileSet, timeStep, timeStepInFile, fileNum;
  vtkDataArray* times;
  vtkIdList *numStepsList, *filenameNumbers;
  float newTime;
  int numSteps;
  char* fileName;
  int filenameNum;

  if (!this->CaseFileRead)
  {
    vtkErrorMacro("error reading case file");
    return 0;
  }

  this->NumberOfNewOutputs = 0;
  this->NumberOfGeometryParts = 0;
  if (this->GeometryFileName)
  {
    timeStep = timeStepInFile = 1;
    fileNum = 1;
    fileName = new char[strlen(this->GeometryFileName) + 10];
    strcpy(fileName, this->GeometryFileName);

    if (this->UseTimeSets)
    {
      timeSet = this->TimeSetIds->IsId(this->GeometryTimeSet);
      if (timeSet >= 0)
      {
        times = this->TimeSets->GetItem(timeSet);
        this->GeometryTimeValue = times->GetComponent(0, 0);
        for (i = 1; i < times->GetNumberOfTuples(); i++)
        {
          newTime = times->GetComponent(i, 0);
          if (newTime <= this->ActualTimeValue && newTime > this->GeometryTimeValue)
          {
            this->GeometryTimeValue = newTime;
            timeStep++;
            timeStepInFile++;
          }
        }
        if (this->TimeSetFileNameNumbers->GetNumberOfItems() > 0)
        {
          int collectionNum = this->TimeSetsWithFilenameNumbers->IsId(this->GeometryTimeSet);
          if (collectionNum > -1)
          {
            filenameNumbers = this->TimeSetFileNameNumbers->GetItem(collectionNum);
            filenameNum = filenameNumbers->GetId(timeStep - 1);
            if (!this->UseFileSets)
            {
              vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName, filenameNum);
            }
          }
        }

        // There can only be file sets if there are also time sets.
        if (this->UseFileSets)
        {
          fileSet = this->FileSets->IsId(this->GeometryFileSet);
          numStepsList =
            static_cast<vtkIdList*>(this->FileSetNumberOfSteps->GetItemAsObject(fileSet));

          if (timeStep > numStepsList->GetId(0))
          {
            numSteps = numStepsList->GetId(0);
            timeStepInFile -= numSteps;
            fileNum = 2;
            for (i = 1; i < numStepsList->GetNumberOfIds(); i++)
            {
              numSteps += numStepsList->GetId(i);
              if (timeStep > numSteps)
              {
                fileNum++;
                timeStepInFile -= numStepsList->GetId(i);
              }
            }
          }
          if (this->FileSetFileNameNumbers->GetNumberOfItems() > 0)
          {
            int collectionNum = this->FileSetsWithFilenameNumbers->IsId(this->GeometryFileSet);
            if (collectionNum > -1)
            {
              filenameNumbers = this->FileSetFileNameNumbers->GetItem(collectionNum);
              filenameNum = filenameNumbers->GetId(fileNum - 1);
              vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName, filenameNum);
            }
          }
        }
      }
    }

    if (!this->ReadGeometryFile(fileName, timeStepInFile, output))
    {
      vtkErrorMacro("error reading geometry file");
      delete[] fileName;
      return 0;
    }

    delete[] fileName;
  }
  if (this->MeasuredFileName)
  {
    timeStep = timeStepInFile = 1;
    fileNum = 1;
    fileName = new char[strlen(this->MeasuredFileName) + 10];
    strcpy(fileName, this->MeasuredFileName);

    if (this->UseTimeSets)
    {
      timeSet = this->TimeSetIds->IsId(this->MeasuredTimeSet);
      if (timeSet >= 0)
      {
        times = this->TimeSets->GetItem(timeSet);
        this->MeasuredTimeValue = times->GetComponent(0, 0);
        for (i = 1; i < times->GetNumberOfTuples(); i++)
        {
          newTime = times->GetComponent(i, 0);
          if (newTime <= this->ActualTimeValue && newTime > this->MeasuredTimeValue)
          {
            this->MeasuredTimeValue = newTime;
            timeStep++;
            timeStepInFile++;
          }
        }
        if (this->TimeSetFileNameNumbers->GetNumberOfItems() > 0)
        {
          int collectionNum = this->TimeSetsWithFilenameNumbers->IsId(this->MeasuredTimeSet);
          if (collectionNum > -1)
          {
            filenameNumbers = this->TimeSetFileNameNumbers->GetItem(collectionNum);
            filenameNum = filenameNumbers->GetId(timeStep - 1);
            if (!this->UseFileSets)
            {
              vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName, filenameNum);
            }
          }
        }

        // There can only be file sets if there are also time sets.
        if (this->UseFileSets)
        {
          fileSet = this->FileSets->IsId(this->MeasuredFileSet);
          numStepsList =
            static_cast<vtkIdList*>(this->FileSetNumberOfSteps->GetItemAsObject(fileSet));

          if (timeStep > numStepsList->GetId(0))
          {
            numSteps = numStepsList->GetId(0);
            timeStepInFile -= numSteps;
            fileNum = 2;
            for (i = 1; i < numStepsList->GetNumberOfIds(); i++)
            {
              numSteps += numStepsList->GetId(i);
              if (timeStep > numSteps)
              {
                fileNum++;
                timeStepInFile -= numStepsList->GetId(i);
              }
            }
          }
          if (this->FileSetFileNameNumbers->GetNumberOfItems() > 0)
          {
            int collectionNum = this->FileSetsWithFilenameNumbers->IsId(this->MeasuredFileSet);
            if (collectionNum > -1)
            {
              filenameNumbers = this->FileSetFileNameNumbers->GetItem(fileSet);
              filenameNum = filenameNumbers->GetId(fileNum - 1);
              vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName, filenameNum);
            }
          }
        }
      }
    }
    if (!this->ReadMeasuredGeometryFile(fileName, timeStepInFile, output))
    {
      vtkErrorMacro("error reading measured geometry file");
      delete[] fileName;
      return 0;
    }
    delete[] fileName;
  }
  if ((this->NumberOfVariables + this->NumberOfComplexVariables) > 0)
  {
    if (!this->ReadVariableFiles(output))
    {
      vtkErrorMacro("error reading variable files");
      return 0;
    }
  }

  return 1;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::RequestInformation(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** vtkNotUsed(inputVector), vtkInformationVector* outputVector)
{
  vtkDebugMacro("In execute information");
  this->CaseFileRead = this->ReadCaseFile();

  // the rigid body files need to be read here because it's possible that there's no time step
  // information in the rest of the files, so we'll need to use the info in the eet file to get
  // time values.
  if (this->RigidBodyFileName)
  {
    if (!this->ReadRigidBodyGeometryFile())
    {
      vtkErrorMacro("Error reading rigid body file. Will attempt to continue reading EnSight "
                    "files, without applying rigid body transformations.");
    }
  }

  // Convert time steps to one sorted and uniquefied list.
  std::vector<double> timeValues;
  if (this->GetTimeSets())
  {
    int numItems = this->GetTimeSets()->GetNumberOfItems();
    for (int i = 0; i < numItems; i++)
    {
      vtkDataArray* array = this->GetTimeSets()->GetItem(i);
      if (array)
      {
        vtkIdType numTuples = array->GetNumberOfTuples();
        for (vtkIdType j = 0; j < numTuples; j++)
        {
          timeValues.push_back(array->GetComponent(j, 0));
        }
      }
    }
  }

  if (timeValues.empty() && this->UseEulerTimeSteps)
  {
    // we'll fall back on using time step info from rigid body files
    if (this->EulerTimeSteps)
    {
      vtkIdType numTuples = this->EulerTimeSteps->GetNumberOfTuples();
      for (vtkIdType i = 0; i < numTuples; i++)
      {
        timeValues.push_back(this->EulerTimeSteps->GetComponent(i, 0));
      }
    }
    else
    {
      vtkErrorMacro("UseEulerTimeSteps is true, but there are no time steps saved.");
      return 0;
    }
  }

  if (!timeValues.empty())
  {
    std::sort(timeValues.begin(), timeValues.end());
    std::vector<double> uniqueTimeValues(
      timeValues.begin(), std::unique(timeValues.begin(), timeValues.end()));
    int numTimeValues = static_cast<int>(uniqueTimeValues.size());
    if (numTimeValues > 0)
    {
      vtkInformation* outInfo = outputVector->GetInformationObject(0);
      outInfo->Set(
        vtkStreamingDemandDrivenPipeline::TIME_STEPS(), uniqueTimeValues.data(), numTimeValues);
      double timeRange[2];
      timeRange[0] = uniqueTimeValues[0];
      timeRange[1] = uniqueTimeValues[numTimeValues - 1];
      outInfo->Set(vtkStreamingDemandDrivenPipeline::TIME_RANGE(), timeRange, 2);
    }
  }
  return this->CaseFileRead;
}

//-----------------------------------------------------------------------------
int vtkEnSightReader::ReadCaseFileScripts(char* line)
{
  /* The scripts variable is a new area of the EnSight Gold Format which is used
  to reference an eventual metadata xml file for handling units. The function
  just skips it.*/
  int lineRead = this->ReadNextDataLine(line);
  while (lineRead)
  {
    if (strncmp(line, "metadata:", 9) == 0)
    {
      vtkDebugMacro("Skipping metadata");
    }
    lineRead = this->ReadNextDataLine(line);
  }
  return lineRead;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ReadCaseFileGeometry(char* line)
{
  char subLine[256];
  int timeSet, fileSet, lineRead;

  // There will definitely be a "model" line.  There may also be "measured"
  // and "match" lines.
  lineRead = this->ReadNextDataLine(line);
  while (lineRead &&
    (strncmp(line, "m", 1) == 0 || strncmp(line, "boundary:", 9) == 0 ||
      strncmp(line, "rigid_body:", 11) == 0))
  {
    if (strncmp(line, "model:", 6) == 0)
    {
      if (sscanf(line, " %*s %d%*[ \t]%d%*[ \t]%[^\t\r\n]", &timeSet, &fileSet, subLine) == 3)
      {
        this->GeometryTimeSet = timeSet;
        this->GeometryFileSet = fileSet;
        this->SetGeometryFileName(subLine);
        vtkDebugMacro(<< this->GetGeometryFileName());
      }
      else if (sscanf(line, " %*s %d%*[ \t]%[^\t\r\n]", &timeSet, subLine) == 2)
      {
        this->GeometryTimeSet = timeSet;
        this->SetGeometryFileName(subLine);
        vtkDebugMacro(<< this->GetGeometryFileName());
      }
      else if (sscanf(line, " %*s %[^\t\r\n]", subLine) == 1)
      {
        this->SetGeometryFileName(subLine);
        vtkDebugMacro(<< this->GetGeometryFileName());
      }
    }
    else if (strncmp(line, "measured:", 9) == 0)
    {
      if (sscanf(line, " %*s %d%*[ \t]%d%*[ \t]%[^\t\r\n]", &timeSet, &fileSet, subLine) == 3)
      {
        this->MeasuredTimeSet = timeSet;
        this->MeasuredFileSet = fileSet;
        this->SetMeasuredFileName(subLine);
        vtkDebugMacro(<< this->GetMeasuredFileName());
      }
      else if (sscanf(line, " %*s %d%*[ \t]%[^\t\r\n]", &timeSet, subLine) == 2)
      {
        this->MeasuredTimeSet = timeSet;
        this->SetMeasuredFileName(subLine);
        vtkDebugMacro(<< this->GetMeasuredFileName());
      }
      else if (sscanf(line, " %*s %[^\t\r\n]", subLine) == 1)
      {
        this->SetMeasuredFileName(subLine);
        vtkDebugMacro(<< this->GetMeasuredFileName());
      }
    }
    else if (strncmp(line, "match:", 6) == 0)
    {
      sscanf(line, " %*s %[^\t\r\n]", subLine);
      this->SetMatchFileName(subLine);
      vtkDebugMacro(<< this->GetMatchFileName());
    }
    else if (strncmp(line, "boundary", 8) == 0)
    {
      // boundaries is just ignored for now
      vtkWarningMacro(<< "boundary file: " << line
                      << " won't be read, because it is not supported yet.");
    }
    else if (strncmp(line, "rigid_body", 10) == 0)
    {
      sscanf(line, " %*s %[^\t\r\n]", subLine);
      this->SetRigidBodyFileName(subLine);
      vtkDebugMacro(<< this->GetRigidBodyFileName());
    }
    lineRead = this->ReadNextDataLine(line);
  }

  return lineRead;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ReadCaseFileVariable(char* line)
{
  char subLine[256], subLine2[256];
  int timeSet, fileSet, lineRead;

  this->NumberOfScalarsPerNode = 0;
  this->NumberOfVectorsPerNode = 0;
  this->NumberOfTensorsAsymPerNode = 0;
  this->NumberOfTensorsSymmPerNode = 0;
  this->NumberOfScalarsPerElement = 0;
  this->NumberOfVectorsPerElement = 0;
  this->NumberOfTensorsAsymPerElement = 0;
  this->NumberOfTensorsSymmPerElement = 0;
  this->NumberOfScalarsPerMeasuredNode = 0;
  this->NumberOfVectorsPerMeasuredNode = 0;
  this->NumberOfComplexScalarsPerNode = 0;
  this->NumberOfComplexVectorsPerNode = 0;
  this->NumberOfComplexScalarsPerElement = 0;
  this->NumberOfComplexVectorsPerElement = 0;

  lineRead = this->ReadNextDataLine(line);
  while (lineRead && strncmp(line, "FORMAT", 6) != 0 && strncmp(line, "GEOMETRY", 8) != 0 &&
    strncmp(line, "VARIABLE", 8) != 0 && strncmp(line, "TIME", 4) != 0 &&
    strncmp(line, "FILE", 4) != 0 && strncmp(line, "SCRIPTS", 7) != 0)
  {
    if (strncmp(line, "constant", 8) == 0)
    {
      vtkDebugMacro(<< line);
    }
    else if (strncmp(line, "scalar", 6) == 0)
    {
      sscanf(line, " %*s %*s %s", subLine);
      if (strcmp(subLine, "node:") == 0)
      {
        vtkDebugMacro("scalar per node");
        this->VariableMode = vtkEnSightReader::SCALAR_PER_NODE;
        if (sscanf(line, " %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->VariableFileSetIds->InsertNextId(fileSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*d %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %d %s", &timeSet, subLine) == 2)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %s", subLine) == 1)
        {
          this->VariableTimeSetIds->InsertNextId(1);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %[^\t\r\n]", subLine);
        }
        this->AddVariableType();
        this->NumberOfScalarsPerNode++;
      }
      else if (strcmp(subLine, "element:") == 0)
      {
        vtkDebugMacro("scalar per element");
        this->VariableMode = vtkEnSightReader::SCALAR_PER_ELEMENT;
        if (sscanf(line, " %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->VariableFileSetIds->InsertNextId(fileSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*d %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %d %s", &timeSet, subLine) == 2)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %s", subLine) == 1)
        {
          this->VariableTimeSetIds->InsertNextId(1);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %[^\t\r\n]", subLine);
        }
        this->AddVariableType();
        this->NumberOfScalarsPerElement++;
      }
      else if (strcmp(subLine, "measured") == 0)
      {
        vtkDebugMacro("scalar per measured node");
        this->VariableMode = vtkEnSightReader::SCALAR_PER_MEASURED_NODE;
        if (sscanf(line, " %*s %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->VariableFileSetIds->InsertNextId(fileSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*d %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %*s %d %s", &timeSet, subLine) == 2)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %*s %s", subLine) == 1)
        {
          this->VariableTimeSetIds->InsertNextId(1);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*s %[^\t\r\n]", subLine);
        }
        this->AddVariableType();
        this->NumberOfScalarsPerMeasuredNode++;
      }
      this->AddVariableFileName(subLine);
      this->NumberOfVariables++;
    }
    else if (strncmp(line, "vector", 6) == 0)
    {
      sscanf(line, " %*s %*s %s", subLine);
      if (strcmp(subLine, "node:") == 0)
      {
        vtkDebugMacro("vector per node");
        this->VariableMode = vtkEnSightReader::VECTOR_PER_NODE;
        if (sscanf(line, " %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->VariableFileSetIds->InsertNextId(fileSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*d %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %d %s", &timeSet, subLine) == 2)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %s", subLine) == 1)
        {
          this->VariableTimeSetIds->InsertNextId(1);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %[^\t\r\n]", subLine);
        }
        this->AddVariableType();
        this->NumberOfVectorsPerNode++;
      }
      else if (strcmp(subLine, "element:") == 0)
      {
        vtkDebugMacro("vector per element");
        this->VariableMode = vtkEnSightReader::VECTOR_PER_ELEMENT;
        if (sscanf(line, " %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->VariableFileSetIds->InsertNextId(fileSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*d %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %d %s", &timeSet, subLine) == 2)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %s", subLine) == 1)
        {
          this->VariableTimeSetIds->InsertNextId(1);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %[^\t\r\n]", subLine);
        }
        this->AddVariableType();
        this->NumberOfVectorsPerElement++;
      }
      else if (strcmp(subLine, "measured") == 0)
      {
        vtkDebugMacro("vector per measured node");
        this->VariableMode = vtkEnSightReader::VECTOR_PER_MEASURED_NODE;
        if (sscanf(line, " %*s %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->VariableFileSetIds->InsertNextId(fileSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*d %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %*s %d %s", &timeSet, subLine) == 2)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %*s %s", subLine) == 1)
        {
          this->VariableTimeSetIds->InsertNextId(1);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*s %[^\t\r\n]", subLine);
        }
        this->AddVariableType();
        this->NumberOfVectorsPerMeasuredNode++;
      }
      this->AddVariableFileName(subLine);
      this->NumberOfVariables++;
    }
    else if (strncmp(line, "tensor", 6) == 0)
    {
      // According to EnSight documentation tensor entry should be of the form:
      // tensor symm per node/element
      // but it seems like you can also find:
      // tensor per node/element
      // Let handle this case here:
      char symm[10];
      char per[10];
      bool asym = false;
      if (sscanf(line, " %*s %s %s %s", symm, per, subLine) != 3)
      {
        vtkErrorMacro("Error while reading: " << line);
      }
      if (!((strcmp(symm, "symm") == 0 || strcmp(symm, "asym") == 0) && strcmp(per, "per") == 0))
      {
        if (sscanf(line, " %*s %s %s", per, subLine) != 2)
        {
          vtkErrorMacro("Error while reading: " << line);
        }
        if (strcmp(per, "per") == 0)
        {
          // Not valid file but seems alright, only 'symm' is missing
          vtkWarningMacro(
            "Looks almost like a valid case file, continuing assuming a symmetric tensor");
        }
        else
        {
          vtkErrorMacro("Trouble reading: " << line);
        }
      }
      if (strcmp(symm, "asym") == 0)
      {
        asym = true;
      }
      if (strcmp(subLine, "node:") == 0)
      {
        vtkDebugMacro("tensor symm per node");
        this->VariableMode =
          asym ? vtkEnSightReader::TENSOR_ASYM_PER_NODE : vtkEnSightReader::TENSOR_SYMM_PER_NODE;
        if (sscanf(line, " %*s %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->VariableFileSetIds->InsertNextId(fileSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*d %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %*s %d %s", &timeSet, subLine) == 2)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %*s %s", subLine) == 1)
        {
          this->VariableTimeSetIds->InsertNextId(1);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*s %[^\t\r\n]", subLine);
        }
        this->AddVariableType();
        if (asym)
        {
          this->NumberOfTensorsAsymPerNode++;
        }
        else
        {
          this->NumberOfTensorsSymmPerNode++;
        }
      }
      else if (strcmp(subLine, "element:") == 0)
      {
        vtkDebugMacro("tensor symm per element");
        this->VariableMode = asym ? vtkEnSightReader::TENSOR_ASYM_PER_ELEMENT
                                  : vtkEnSightReader::TENSOR_SYMM_PER_ELEMENT;
        if (sscanf(line, " %*s %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->VariableFileSetIds->InsertNextId(fileSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*d %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %*s %d %s", &timeSet, subLine) == 2)
        {
          this->VariableTimeSetIds->InsertNextId(timeSet);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*d %*s %[^\t\r\n]", subLine);
        }
        else if (sscanf(line, " %*s %*s %*s %*s %s", subLine) == 1)
        {
          this->VariableTimeSetIds->InsertNextId(1);
          this->AddVariableDescription(subLine);
          sscanf(line, " %*s %*s %*s %*s %*s %[^\t\r\n]", subLine);
        }
        this->AddVariableType();
        if (asym)
        {
          this->NumberOfTensorsAsymPerElement++;
        }
        else
        {
          this->NumberOfTensorsSymmPerElement++;
        }
      }
      else
      {
        vtkErrorMacro("Unknown type, faulty line was:" << line);
      }
      this->AddVariableFileName(subLine);
      this->NumberOfVariables++;
    }
    else if (strncmp(line, "complex", 6) == 0)
    {
      sscanf(line, " %*s %s", subLine);
      if (strcmp(subLine, "scalar") == 0)
      {
        sscanf(line, " %*s %*s %*s %s", subLine);
        if (strcmp(subLine, "node:") == 0)
        {
          vtkDebugMacro("complex scalar per node");
          this->VariableMode = vtkEnSightReader::COMPLEX_SCALAR_PER_NODE;
          if (sscanf(line, " %*s %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(timeSet);
            this->ComplexVariableFileSetIds->InsertNextId(fileSet);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*d %*d %*s %s %s", subLine, subLine2);
          }
          else if (sscanf(line, " %*s %*s %*s %*s %d %s", &timeSet, subLine) == 2)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(timeSet);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*d %*s %s %s", subLine, subLine2);
          }
          else if (sscanf(line, " %*s %*s %*s %*s %s", subLine) == 1)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(1);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*s %s %s", subLine, subLine2);
          }
          this->AddVariableType();
          this->NumberOfComplexScalarsPerNode++;
        }
        else if (strcmp(subLine, "element:") == 0)
        {
          vtkDebugMacro("complex scalar per element");
          this->VariableMode = vtkEnSightReader::COMPLEX_SCALAR_PER_ELEMENT;
          if (sscanf(line, " %*s %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(timeSet);
            this->ComplexVariableFileSetIds->InsertNextId(fileSet);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*d %*d %*s %s %s", subLine, subLine2);
          }
          else if (sscanf(line, " %*s %*s %*s %*s %d %s", &timeSet, subLine) == 2)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(timeSet);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*d %*s %s %s", subLine, subLine2);
          }
          else if (sscanf(line, " %*s %*s %*s %*s %s", subLine) == 1)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(1);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*s %s %s", subLine, subLine2);
          }
          this->AddVariableType();
          this->NumberOfComplexScalarsPerElement++;
        }
      }
      else if (strcmp(subLine, "vector") == 0)
      {
        sscanf(line, " %*s %*s %*s %s", subLine);
        if (strcmp(subLine, "node:") == 0)
        {
          vtkDebugMacro("complex vector per node");
          this->VariableMode = vtkEnSightReader::COMPLEX_VECTOR_PER_NODE;
          if (sscanf(line, " %*s %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(timeSet);
            this->ComplexVariableFileSetIds->InsertNextId(fileSet);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*d %*d %*s %s %s", subLine, subLine2);
          }
          else if (sscanf(line, " %*s %*s %*s %*s %d %s", &timeSet, subLine) == 2)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(timeSet);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*d %*s %s %s", subLine, subLine2);
          }
          else if (sscanf(line, " %*s %*s %*s %*s %s", subLine) == 1)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(1);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*s %s %s", subLine, subLine2);
          }
          this->AddVariableType();
          this->NumberOfComplexVectorsPerNode++;
        }
        else if (strcmp(subLine, "element:") == 0)
        {
          vtkDebugMacro("complex vector per element");
          this->VariableMode = vtkEnSightReader::COMPLEX_VECTOR_PER_ELEMENT;
          if (sscanf(line, " %*s %*s %*s %*s %d %d %s", &timeSet, &fileSet, subLine) == 3)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(timeSet);
            this->ComplexVariableFileSetIds->InsertNextId(fileSet);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*d %*d %*s %s %s", subLine, subLine2);
          }
          else if (sscanf(line, " %*s %*s %*s %*s %d %s", &timeSet, subLine) == 2)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(timeSet);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*d %*s %s %s", subLine, subLine2);
          }
          else if (sscanf(line, " %*s %*s %*s %*s %s", subLine) == 1)
          {
            this->ComplexVariableTimeSetIds->InsertNextId(1);
            this->AddVariableDescription(subLine);
            sscanf(line, " %*s %*s %*s %*s %*s %s %s", subLine, subLine2);
          }
          this->AddVariableType();
          this->NumberOfComplexVectorsPerElement++;
        }
      }
      this->AddVariableFileName(subLine, subLine2);
      this->NumberOfComplexVariables++;
    }
    else
    {
      vtkErrorMacro("invalid VARIABLE line: " << line);
      delete this->IS;
      this->IS = nullptr;
      return 0;
    }
    lineRead = this->ReadNextDataLine(line);
  }

  // Fill data array selection objects with these arrays.
  this->SetDataArraySelectionSetsFromVariables();
  return lineRead;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ReadCaseFileTime(char* line)
{
  char formatLine[256];
  char subLine[256];
  int timeSet, numTimeSteps, i, filenameNum, increment, lineRead;
  float timeStep;

  // found TIME section
  int firstTimeStep = 1;

  this->UseTimeSetsOn();
  int lineScanResult = 0;
  lineRead = this->ReadNextDataLine(line);
  while (lineRead && strncmp(line, "FORMAT", 6) != 0 && strncmp(line, "GEOMETRY", 8) != 0 &&
    strncmp(line, "VARIABLE", 8) != 0 && strncmp(line, "TIME", 4) != 0 &&
    strncmp(line, "FILE", 4) != 0)
  {
    // 'time set: <int>' --- to obtain timeSet, an index
    lineScanResult = sscanf(line, "%*s %s %d", subLine, &timeSet);
    if (lineScanResult != 2 || strncmp(line, "time", 4) != 0 || strcmp(subLine, "set:") != 0)
    {
      vtkErrorMacro("Error with vtkEnSightReader: 'time set' not found!!!");
      return 0;
    }
    this->TimeSetIds->InsertNextId(timeSet);

    // 'number of steps: <int>' --- to obtain numTimeSteps
    // i.e.., the number of 'filename numbers' and equally that of 'time values'
    if (this->ReadNextDataLine(line) == 0)
    {
      vtkErrorMacro("Error with vtkEnSightReader: 'number of steps' not found!!!");
      return 0;
    }

    lineScanResult = sscanf(line, "%*s %*s %s %d", subLine, &numTimeSteps);
    if (lineScanResult != 2 || strncmp(line, "number", 6) != 0 || strcmp(subLine, "steps:") != 0)
    {
      vtkErrorMacro("Error with vtkEnSightReader: 'number of steps' not found!!!");
      return 0;
    }

    // 'filename numbers:' ==or the combination of the following two lines==
    // 'filename start number: <int>'
    // 'filename increment: <int>'
    // --- to obtain a sequence of filenameNum(s) which might span multiple lines
    if (this->ReadNextDataLine(line) == 0)
    {
      vtkErrorMacro("Error with vtkEnSightReader: 'filename ......' entry not found!!!");
      return 0;
    }

    if (strncmp(line, "filename", 8) == 0)
    {
      vtkIdList* filenameNumbers = vtkIdList::New();
      this->TimeSetsWithFilenameNumbers->InsertNextId(timeSet);

      if (sscanf(line, "%*s %s", subLine) != 1)
      {
        vtkErrorMacro("Error with vtkEnSightReader: 'filename ......' not found!!!");
        return 0;
      }

      // 'filename numbers:'
      if (strncmp(subLine, "numbers", 7) == 0)
      {
        // Filename numbers may be provided on the line(s) following
        // 'filename numbers:', as is usually the case --- not "inline". Thus we need
        // to go to the FIRST line that indeed contains filename numbers.
        if (sscanf(line, "%*s %*s %d", &filenameNum) != 1)
        {
          // not "inline"
          if (this->ReadNextDataLine(line) == 0)
          {
            vtkErrorMacro("Error with vtkEnSightReader: filename numbers not found!!!");
            return 0;
          }
          // access the sub-strings from the very beginning
          strcpy(formatLine, "");
          strcpy(subLine, "");
        }
        else
        {
          // "inline" ----> skip the first two sub-strings: 'filename numbers:'
          strcpy(formatLine, "%*s %*s ");
          strcpy(subLine, "%*s %*s ");
        }

        for (i = 0; i < numTimeSteps; i++)
        {
          strcat(formatLine, "%d ");

          // More lines might be needed to provide the remaining filename numbers
          // and then formatLine and subLine need to be updated. 'while' is used here
          // instead of 'if' in case of any invalid filename numbers.
          while (sscanf(line, formatLine, &filenameNum) != 1)
          {
            if (this->ReadNextDataLine(line) == 0)
            {
              vtkErrorMacro("Error with vtkEnSightReader: insufficient filename numbers!!!");
              return 0;
            }

            // in case of insufficient filename numbers
            if (strncmp(line, "filename start", 14) == 0 ||
              strncmp(line, "filename increment", 18) == 0 ||
              strncmp(line, "time values", 11) == 0 || strncmp(line, "time set", 8) == 0 ||
              strncmp(line, "FILE", 4) == 0)
            {
              vtkErrorMacro("Error with vtkEnSightReader: insufficient filename numbers!!!");
              return 0;
            }

            // to access a new line
            strcpy(formatLine, "%d ");
            strcpy(subLine, "");
          }

          filenameNumbers->InsertNextId(filenameNum);
          strcat(subLine, "%*d ");
          strcpy(formatLine, subLine);
        }
      }
      else
      // subLine == "start" ----> 'filename start number: <int>' followed by
      // 'filename increment: <int>'
      {
        if (strcmp(subLine, "start") != 0 || sscanf(line, "%*s %*s %*s %d", &filenameNum) != 1)
        {
          vtkErrorMacro("Error with vtkEnSightReader: 'filename start number' not found!!!");
          return 0;
        }

        if (this->ReadNextDataLine(line) == 0)
        {
          vtkErrorMacro("Error with vtkEnSightReader: 'filename increment' not found!!!");
          return 0;
        }

        lineScanResult = sscanf(line, "%*s %s %d", subLine, &increment);
        if (lineScanResult != 2 || strcmp(subLine, "increment:") != 0)
        {
          vtkErrorMacro("Error with vtkEnSightReader: 'filename increment' not found!!!");
          return 0;
        }

        for (i = 0; i < numTimeSteps; i++)
        {
          filenameNumbers->InsertNextId(filenameNum + i * increment);
        }
      }
      this->TimeSetFileNameNumbers->AddItem(filenameNumbers);
      filenameNumbers->Delete();

      // To ignore redundant filename numbers, just if any,
      // and check if 'time values' are subsequently provided as expected.
      do
      {
        lineRead = this->ReadNextDataLine(line);
      } while (lineRead != 0 && strncmp(line, "time values", 11) != 0);

      if (lineRead == 0)
      {
        vtkErrorMacro("Error with vtkEnSightReader: 'time values' not found!!!");
        return 0;
      }
    }

    // 'time values:' --- to obtain timeStep(s)
    vtkFloatArray* timeValues = vtkFloatArray::New();
    timeValues->SetNumberOfComponents(1);
    timeValues->SetNumberOfTuples(numTimeSteps);

    // Time values may be provided on the line(s) following  'time values:',
    // as is usually the case --- not "inline". Thus we need to go to the
    // FIRST line that indeed contains time values.
    if (sscanf(line, "%*s %*s %f", &timeStep) != 1)
    {
      // not "inline"
      if (this->ReadNextDataLine(line) == 0)
      {
        vtkErrorMacro("Error with vtkEnSightReader: time values not found!!!");
        return 0;
      }
      // access the sub-strings from the very beginning
      strcpy(formatLine, "");
      strcpy(subLine, "");
    }
    else
    {
      // "inline" ----> skip the first two sub-strings: 'time values:'
      strcpy(formatLine, "%*s %*s ");
      strcpy(subLine, "%*s %*s ");
    }

    for (i = 0; i < numTimeSteps; i++)
    {
      strcat(formatLine, "%f ");

      // More lines might be needed to provide the remaining time values
      // and then formatLine and subLine need to be updated. 'while' is used
      // here instead of 'if' in case of any invalid time values.
      while (sscanf(line, formatLine, &timeStep) != 1)
      {
        if (this->ReadNextDataLine(line) == 0)
        {
          vtkErrorMacro("Error with vtkEnSightReader: insufficient time values!!!");
          return 0;
        }

        // in case of insufficient time values
        if (strncmp(line, "time set", 8) == 0 || strncmp(line, "FILE", 4) == 0)
        {
          vtkErrorMacro("Error with vtkEnSightReader: insufficient time values!!!");
          return 0;
        }

        // to access a new line
        strcpy(formatLine, "%f ");
        strcpy(subLine, "");
      }

      timeValues->SetComponent(i, 0, timeStep);
      strcat(subLine, "%*f ");
      strcpy(formatLine, subLine);

      // init min and max only upon the access to the FIRST 'timeStep'
      if (firstTimeStep)
      {
        this->MinimumTimeValue = timeStep;
        this->MaximumTimeValue = timeStep;
        firstTimeStep = 0;
        // Set this as default TimeValue.
        if (!this->TimeValueInitialized)
        {
          this->SetTimeValue(timeStep);
        }
      }
      else
      {
        if (timeStep < this->MinimumTimeValue)
        {
          this->MinimumTimeValue = timeStep;
        }
        else if (timeStep > this->MaximumTimeValue)
        {
          this->MaximumTimeValue = timeStep;
        }
      }
    }
    this->TimeSets->AddItem(timeValues);
    timeValues->Delete();

    // The following line MUST *NOT* be modified as it enables the loop
    // "while ( lineReadResult != 0 && strncmp(line, "FILE", 4) != 0 )".
    // It also enables the check of redundant time-step values.
    lineRead = this->ReadNextDataLine(line);

    // To ignore redundant time-step values, if any, to fix bug #0007091
    while (lineRead != 0 && strncmp(line, "time set", 8) != 0 && strncmp(line, "FILE", 4) != 0 &&
      strncmp(line, "FORMAT", 6) != 0 && strncmp(line, "GEOMETRY", 8) != 0 &&
      strncmp(line, "VARIABLE", 8) != 0 && strncmp(line, "TIME", 4) != 0)
    {
      lineRead = this->ReadNextDataLine(line);
    }
  }

  return lineRead;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ReadCaseFileFile(char* line)
{
  int fileSet, numTimeSteps, filenameNum, lineRead;

  this->UseFileSetsOn();
  lineRead = this->ReadNextDataLine(line);
  while (lineRead && strncmp(line, "FORMAT", 6) != 0 && strncmp(line, "GEOMETRY", 8) != 0 &&
    strncmp(line, "VARIABLE", 8) != 0 && strncmp(line, "TIME", 4) != 0 &&
    strncmp(line, "FILE", 4) != 0)
  {
    vtkIdList* filenameNums = vtkIdList::New();
    vtkIdList* numSteps = vtkIdList::New();
    sscanf(line, "%*s %*s %d", &fileSet);
    this->FileSets->InsertNextId(fileSet);
    lineRead = this->ReadNextDataLine(line);
    if (strncmp(line, "filename", 8) == 0)
    {
      this->FileSetsWithFilenameNumbers->InsertNextId(fileSet);
      while (lineRead != 0 && strncmp(line, "filename", 8) == 0)
      {
        sscanf(line, "%*s %*s %d", &filenameNum);
        filenameNums->InsertNextId(filenameNum);
        this->ReadNextDataLine(line);
        sscanf(line, "%*s %*s %*s %d", &numTimeSteps);
        numSteps->InsertNextId(numTimeSteps);
        lineRead = this->ReadNextDataLine(line);
      }
      this->FileSetFileNameNumbers->AddItem(filenameNums);
    }
    else
    {
      sscanf(line, "%*s %*s %*s %d", &numTimeSteps);
      numSteps->InsertNextId(numTimeSteps);
      lineRead = this->ReadNextDataLine(line);
    }

    this->FileSetNumberOfSteps->AddItem(numSteps);

    filenameNums->Delete();
    numSteps->Delete();
  }

  return lineRead;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ReadCaseFile()
{
  char line[256];
  char subLine[256];
  int stringRead;
  int i;
  int ret;
  vtkDebugMacro("In vtkEnSightReader::ReadCaseFile");

  // Initialize
  //
  if (!this->CaseFileName)
  {
    vtkErrorMacro("A CaseFileName must be specified.");
    return 0;
  }
  std::string sfilename;
  if (this->FilePath)
  {
    sfilename = this->FilePath;
    if (sfilename.at(sfilename.length() - 1) != '/')
    {
      sfilename += "/";
    }
    sfilename += this->CaseFileName;
    vtkDebugMacro("full path to case file: " << sfilename);
  }
  else
  {
    sfilename = this->CaseFileName;
  }

  this->IS = new vtksys::ifstream(sfilename.c_str(), ios::in);
  if (this->IS->fail())
  {
    vtkErrorMacro("Unable to open file: " << sfilename);
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  this->TimeSets->RemoveAllItems();

  for (i = 0; i < this->NumberOfVariables; i++)
  {
    delete[] this->VariableFileNames[i];
    this->VariableFileNames[i] = nullptr;
    delete[] this->VariableDescriptions[i];
    this->VariableDescriptions[i] = nullptr;
  }
  delete[] this->VariableFileNames;
  this->VariableFileNames = nullptr;
  delete[] this->VariableDescriptions;
  this->VariableDescriptions = nullptr;
  delete[] this->VariableTypes;
  this->VariableTypes = nullptr;

  for (i = 0; i < this->NumberOfComplexVariables; i++)
  {
    delete[] this->ComplexVariableFileNames[2 * i];
    this->ComplexVariableFileNames[2 * i] = nullptr;
    delete[] this->ComplexVariableFileNames[2 * i + 1];
    this->ComplexVariableFileNames[2 * i + 1] = nullptr;
    delete[] this->ComplexVariableDescriptions[i];
    this->ComplexVariableDescriptions[i] = nullptr;
  }
  delete[] this->ComplexVariableFileNames;
  this->ComplexVariableFileNames = nullptr;
  delete[] this->ComplexVariableDescriptions;
  this->ComplexVariableDescriptions = nullptr;
  delete[] this->ComplexVariableTypes;
  this->ComplexVariableTypes = nullptr;

  this->NumberOfVariables = 0;
  this->NumberOfComplexVariables = 0;

  this->ReadNextDataLine(line);

  if (strncmp(line, "FORMAT", 6) == 0)
  {
    // found the FORMAT section
    vtkDebugMacro("*** FORMAT section");
    this->ReadNextDataLine(line);

    stringRead = sscanf(line, " %*s %*s %s", subLine);
    if (stringRead == 1)
    {
      if (strcmp(subLine, "gold") == 0 && strcmp(this->GetClassName(), "vtkEnSight6Reader") == 0)
      {
        // The class is vtkEnSight6Reader, but the case file says "gold".
        vtkErrorMacro("This is not an EnSight6 file.");
        delete this->IS;
        this->IS = nullptr;
        return 0;
      }
    }
    else
    {
      if (strcmp(this->GetClassName(), "vtkEnSightGoldReader") == 0)
      {
        // The class is vtkEnSightGoldReader, but the case file does
        // not say "gold".
        vtkErrorMacro("This is not an EnSight Gold file.");
        delete this->IS;
        this->IS = nullptr;
        return 0;
      }
    }
  }

  ret = this->ReadNextDataLine(line);
  // We know how many lines to read in the FORMAT section, so we haven't read
  // the "GEOMETRY" line yet.
  while (ret)
  {
    if (strncmp(line, "GEOMETRY", 8) == 0)
    {
      // found the GEOMETRY section
      vtkDebugMacro("*** GEOMETRY section");
      ret = this->ReadCaseFileGeometry(line);
    }
    else if (strncmp(line, "VARIABLE", 8) == 0)
    {
      // found the VARIABLE section
      vtkDebugMacro(<< "*** VARIABLE section");
      ret = this->ReadCaseFileVariable(line);
    }
    else if (strncmp(line, "TIME", 4) == 0)
    {
      // found TIME section
      // 'TIME' section includes the following sub-sections
      // 'time set: <int>'
      // 'number of steps: <int>'
      // 'filename numbers:' --- either inline or not
      // 'filename start number: <int>' --- 'start' may be combined with 'increment'
      // 'filename increment: <int>'    --- to serve as an alternative to 'numbers'
      // 'time values:' --- either inline or not
      vtkDebugMacro(<< "*** TIME section");
      ret = this->ReadCaseFileTime(line);
    }
    else if (strncmp(line, "FILE", 4) == 0)
    {
      // found FILE section
      vtkDebugMacro(<< "*** FILE section");
      ret = this->ReadCaseFileFile(line);
    }
    else if (strncmp(line, "SCRIPTS", 7) == 0)
    {
      // found SCRIPTS section
      vtkDebugMacro(<< "*** SCRIPTS section");
      ret = this->ReadCaseFileScripts(line);
    }
  }

  delete this->IS;
  this->IS = nullptr;

  // Fill data array selection objects with these arrays.
  // TODO: Segfault or not segfault ? That is the question...
  this->SetDataArraySelectionSetsFromVariables();
  return 1;
  //  return ret;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ReadRigidBodyMatrixLines(
  char* line, vtkTransform* transform, bool& applyToVectors)
{
  vtkDebugMacro("Reading matrix lines");
  // reads all 4 matrix lines into a vtkMatrix4x4 and concatenates it into transform
  char subLine[256];
  subLine[0] = '\0';
  std::string transType;
  double values[4];

  // first line of matrix starts with either 'M:' or 'Mv:'
  if (sscanf(
        line, " %s %lf %lf %lf %lf", subLine, &values[0], &values[1], &values[2], &values[3]) == 5)
  {
    transType = subLine;
  }
  else if (sscanf(line, " %s %lf %lf %lf", subLine, &values[1], &values[2], &values[3]) == 4)
  {
    // there may not be a space between M:/Mv: and the first number
    // so split up subLine to get the transType and values[0]
    auto strParts = vtksys::SystemTools::SplitString(subLine, ':');
    transType = strParts[0];
    values[0] = std::stod(strParts[1]);
  }
  else
  {
    vtkErrorMacro("could not correctly read matrix line for line: " << line);
    return 0;
  }

  if (transType[0] != 'M')
  {
    vtkErrorMacro("The transform type " << transType << " should be a matrix");
    return 0;
  }

  applyToVectors = vtksys::SystemTools::StringStartsWith(transType, "Mv");

  vtkNew<vtkMatrix4x4> matrix;
  for (int row = 0; row < 4; ++row)
  {
    if (row != 0)
    {
      this->ReadNextDataLine(line);
      if (sscanf(line, " %lf %lf %lf %lf", &values[0], &values[1], &values[2], &values[3]) != 4)
      {
        vtkErrorMacro(
          "could not correctly read matrix values for row " << row << " from line " << line);
      }
    }

    for (int col = 0; col < 4; ++col)
    {
      // based on the example in the EnSight user manual, it seems we need to do the
      // transform of the matrix as its given in the erb file
      matrix->SetElement(col, row, values[col]);
    }
  }
  transform->Concatenate(matrix);
  return 1;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ReadRigidBodyGeometryFile()
{
  if (strcmp(this->GetClassName(), "vtkEnSightGoldReader") != 0 &&
    strcmp(this->GetClassName(), "vtkEnSightGoldBinaryReader") != 0)
  {
    vtkErrorMacro("Rigid Body files are only supported for EnSight Gold readers.");
    return 0;
  }

  vtkDebugMacro("Reading rigid body geometry file (erb)");

  char line[256], subLine[256];
  line[0] = '\0';
  subLine[0] = '\0';

  std::string filename(this->RigidBodyFileName);
  std::string sfilename;
  this->SanitizeFileName(filename);
  if (this->FilePath)
  {
    sfilename = this->FilePath;
    if (sfilename.at(sfilename.length() - 1) != '/')
    {
      sfilename += "/";
    }
  }
  sfilename += filename;
  vtkDebugMacro("full path to rigid body geometry file: " << sfilename);

  this->IS = new vtksys::ifstream(sfilename.c_str(), ios::in);
  if (this->IS->fail())
  {
    vtkErrorMacro("Unable to open file: " << sfilename);
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  this->RigidBodyTransforms.clear();
  this->EulerTransformsMap.clear();
  this->UseEulerTimeSteps = false;
  if (this->EulerTimeSteps)
  {
    this->EulerTimeSteps->SetNumberOfTuples(0);
  }

  // this should be EnSight Rigid Body
  if (this->ReadNextDataLine(line) == 0 || strncmp(line, "EnSight Rigid Body", 18) != 0)
  {
    vtkErrorMacro("The first line " << line << " is not 'EnSight Rigid Body'.");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  // read the version now
  if (this->ReadNextDataLine(line) == 0 || strncmp(line, "version", 7) != 0)
  {
    vtkErrorMacro("The second line " << line << " does not include 'version'.");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  float version;
  if (sscanf(line, " %*s %f", &version) != 1)
  {
    vtkErrorMacro("version line '" << line << "' does not contain a valid version number");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }
  if (version != 2.0)
  {
    vtkErrorMacro("currently only version 2.0 of the rigid body format is supported.");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  // read "names" or "numbers"
  if (this->ReadNextDataLine(line) == 0)
  {
    vtkErrorMacro("There was an issue reading the names/numbers line");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }
  if (strncmp(line, "names", 5) == 0)
  {
    this->UsePartNamesRB = true;
  }
  else if (strncmp(line, "numbers", 7) == 0)
  {
    this->UsePartNamesRB = false;
  }
  else
  {
    vtkErrorMacro("The third line " << line << " is not 'names' or 'numbers'.");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  if (this->ReadNextDataLine(line) == 0)
  {
    vtkErrorMacro("Error reading line with number of parts");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }
  int numParts = std::stoi(line);

  // read the number of following part names / part numbers
  int lineRead = this->ReadNextDataLine(line); // either a part name or number
  int idx = 0;
  while (lineRead && idx < numParts)
  {
    // handle line which is either a part name or number
    int partId;
    std::string partName;
    if (this->UsePartNamesRB)
    {
      partName = line;
      this->SanitizeFileName(partName);
    }
    else
    {
      // Need to make sure that we remove any quotes from the partId
      partName = line;
      this->SanitizeFileName(partName);
      partId = std::stoi(partName) - 1; // EnSight starts #ing at 1.
      partName = std::to_string(partId);
    }

    this->ReadNextDataLine(line); // num of transformations
    int numTransformations = std::stoi(line);
    vtkDebugMacro("reading transforms for part " << partName << ", which has " << numTransformations
                                                 << " transformations");

    if (this->RigidBodyTransforms.count(partName))
    {
      vtkErrorMacro("Parts should only be listed once in the rigid body file, but part "
        << partName << " has already been read.");
      delete this->IS;
      this->IS = nullptr;
      return 0;
    }
    this->RigidBodyTransforms.insert(std::make_pair(partName, PartTransforms()));
    auto& currentPartTransform = this->RigidBodyTransforms[partName];

    // now loop through transformations
    int transIdx = 0;
    bool pretransform = true;
    while (this->ReadNextDataLine(line) && transIdx < numTransformations)
    {
      if (strncmp(line, "Eul:", 4) == 0)
      {
        // we can't actually read this file yet because ReadNextDataLine
        // operates on this->IS, so we have to read this whole file first,
        // then go back and read the Euler param file
        char title[256];
        title[0] = '\0';
        if (sscanf(line, " %*s %s %s", subLine, title) == 2)
        {
          currentPartTransform.EETFilename = subLine;
          this->SanitizeFileName(currentPartTransform.EETFilename);
          currentPartTransform.EETTransTitle = title;
          this->SanitizeFileName(currentPartTransform.EETTransTitle);
        }
        vtkDebugMacro("Eul section EET file: " << currentPartTransform.EETFilename);
        vtkDebugMacro("EET title: " << currentPartTransform.EETTransTitle);
        pretransform = false;
        transIdx++;
        continue;
      }

      vtkTransform* transform;
      if (pretransform)
      {
        currentPartTransform.PreTransforms.emplace_back(vtkSmartPointer<vtkTransform>::New());
        transform = currentPartTransform.PreTransforms.back();
      }
      else
      {
        currentPartTransform.PostTransforms.emplace_back(vtkSmartPointer<vtkTransform>::New());
        transform = currentPartTransform.PostTransforms.back();
      }
      transform->PostMultiply();
      bool applyToVectors = false;

      if (strncmp(line, "M:", 2) == 0 || strncmp(line, "Mv:", 3) == 0)
      {
        // M matrices applied only to geometry
        // Mv matrices applied to geometry and vectors
        if (!this->ReadRigidBodyMatrixLines(line, transform, applyToVectors))
        {
          // some error happened reading the matrix lines
          delete this->IS;
          this->IS = nullptr;
          return 0;
        }
      }
      else
      {
        // other possibilities are all single values
        // rotations and scaling should be applied to geometry and vectors
        // translations are only applied to geometry
        double value;
        if (sscanf(line, " %s %lf", subLine, &value) != 2)
        {
          vtkErrorMacro("Expected a transformation with a single value for line: " << line);
          delete this->IS;
          this->IS = nullptr;
          return 0;
        }
        std::string transStr(subLine);
        vtkDebugMacro("Found transformation " << transStr << ", with value of " << value);
        if (transStr == "Tx:")
        {
          transform->Translate(value, 0, 0);
          applyToVectors = false;
        }
        else if (transStr == "Ty:")
        {
          transform->Translate(0, value, 0);
          applyToVectors = false;
        }
        else if (transStr == "Tz:")
        {
          transform->Translate(0, 0, value);
          applyToVectors = false;
        }
        else if (transStr == "Sx:")
        {
          transform->Scale(value, 1, 1);
          applyToVectors = true;
        }
        else if (transStr == "Sy:")
        {
          transform->Scale(1, value, 1);
          applyToVectors = true;
        }
        else if (transStr == "Sz:")
        {
          transform->Scale(1, 1, value);
          applyToVectors = true;
        }
        else
        {
          // everything else should be rotation
          // transStr should be one of 'Rx:', 'Ry:', or 'Rz:' if the value is in degrees
          // or 'Rxr:', 'Ryr:', or 'Rzr:' if the value is in radians
          if (transStr[0] != 'R')
          {
            vtkErrorMacro("the transform string " << transStr << " is not valid.");
            delete this->IS;
            this->IS = nullptr;
            return 0;
          }
          applyToVectors = true;

          if (transStr.size() == 4 && transStr[2] == 'r')
          {
            // convert radians to degrees
            value = vtkMath::DegreesFromRadians(value);
          }

          switch (transStr[1])
          {
            case 'x':
              transform->RotateX(value);
              break;
            case 'y':
              transform->RotateY(value);
              break;
            case 'z':
              transform->RotateZ(value);
              break;
            default:
              vtkErrorMacro("couldn't determine rotation type");
          }
        }
      }

      if (pretransform)
      {
        currentPartTransform.PreTransformsApplyToVectors.push_back(applyToVectors);
      }
      else
      {
        currentPartTransform.PostTransformsApplyToVectors.push_back(applyToVectors);
      }

      transIdx++;
    }

    if (currentPartTransform.EETFilename.empty() || currentPartTransform.EETTransTitle.empty())
    {
      vtkErrorMacro("Every part in a rigid body file must have an 'Eul:' line");
      delete this->IS;
      this->IS = nullptr;
      return 0;
    }

    idx++;
    if (!lineRead)
    {
      // last read was EOF
      break;
    }
  }

  // cleanup so we can read the eet_file
  delete this->IS;
  this->IS = nullptr;

  // It's possible that these files could be stored in a different directory from the
  // case file. the erb file will have a path relative to the case file, while the
  // eet file has a path relative to the erb. for example with the following directory:
  // - output.case
  // - data/output.erb
  // - data/output.eet
  // So in the case file, the path to the erb file will say 'data/output.erb'
  // while in the erb file, the eet file will just say 'output.eet'.
  std::vector<std::string> filenameComponents;
  vtksys::SystemTools::SplitPath(filename, filenameComponents);
  auto path =
    vtksys::SystemTools::JoinPath(filenameComponents.begin(), filenameComponents.end() - 1);
  return this->ReadRigidBodyEulerParameterFile(path.c_str());
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ReadRigidBodyEulerParameterFile(const char* path)
{
  if (strcmp(this->GetClassName(), "vtkEnSightGoldReader") != 0 &&
    strcmp(this->GetClassName(), "vtkEnSightGoldBinaryReader") != 0)
  {
    vtkErrorMacro("Rigid Body files are only supported for EnSight Gold readers.");
  }

  vtkDebugMacro("Reading rigid body euler parameter file (eet)");

  char line[256];
  line[0] = '\0';

  // according to EnSight User manual, although the format technically allows for different
  // .eet files for different parts, EnSight can only handle one per model, so we'll just grab
  // the file name info from the first part in this->RigidBodyTransforms.
  // If this changes in a future version, we can update this to read multiple eet files.
  auto filename = this->RigidBodyTransforms.begin()->second.EETFilename;

  if (filename.empty())
  {
    vtkErrorMacro("An euler parameter file must be specified in the rigid body file.");
    return 0;
  }
  std::string sfilename;
  this->SanitizeFileName(filename);
  if (this->FilePath)
  {
    sfilename = this->FilePath;
    if (sfilename.at(sfilename.length() - 1) != '/')
    {
      sfilename += "/";
    }
  }
  sfilename += path;
  if (sfilename.at(sfilename.length() - 1) != '/')
  {
    sfilename += "/";
  }
  sfilename += filename;
  vtkDebugMacro("full path to eet file: " << sfilename);

  this->IS = new vtksys::ifstream(sfilename.c_str(), ios::in);
  if (this->IS->fail())
  {
    std::cout << "Unable to open file: " << sfilename << std::endl;
    vtkErrorMacro("Unable to open file: " << sfilename);
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  // first line should be "Ens_Euler"
  if (this->ReadNextDataLine(line) == 0 || strncmp(line, "Ens_Euler", 9) != 0)
  {
    vtkErrorMacro("The first line " << line << " is not 'Ens_Euler'");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  if (this->ReadNextDataLine(line) == 0 || strncmp(line, "NumTimes:", 9) != 0)
  {
    vtkErrorMacro("The second line " << line << " is not 'NumTimes:'");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  // line should contain the number of time steps in the file
  if (this->ReadNextDataLine(line) == 0)
  {
    vtkErrorMacro("Unable to read number of time steps in eet file");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  int numTimes = std::stoi(line);
  vtkDebugMacro("number of timesteps: " << numTimes);
  // UseTimeSets is set to on in ReadCaseFileTime. If it is off, that means the dataset
  // doesn't have time set info, but the euler transformations provide that
  this->UseEulerTimeSteps = !this->GetUseTimeSets();
  if (this->UseEulerTimeSteps)
  {
    if (!this->EulerTimeSteps)
    {
      this->EulerTimeSteps = vtkSmartPointer<vtkDoubleArray>::New();
    }
    this->EulerTimeSteps->SetNumberOfComponents(1);
    this->EulerTimeSteps->SetNumberOfTuples(numTimes);
  }

  if (this->ReadNextDataLine(line) == 0 || strncmp(line, "NumTrans:", 9) != 0)
  {
    vtkErrorMacro("The line " << line << " should be 'NumTrans:'");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  // line should contain the number of transforms in the file
  if (this->ReadNextDataLine(line) == 0)
  {
    vtkErrorMacro("Unable to read number of transforms in eet file");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  int numTrans = std::stoi(line);
  vtkDebugMacro("number of transforms: " << numTrans);

  if (this->ReadNextDataLine(line) == 0 || strncmp(line, "Titles:", 7) != 0)
  {
    vtkErrorMacro("The line " << line << " should be 'Titles:'");
    delete this->IS;
    this->IS = nullptr;
    return 0;
  }

  std::vector<std::string> titles;
  for (int i = 0; i < numTrans; ++i)
  {
    if (this->ReadNextDataLine(line) == 0)
    {
      vtkErrorMacro("Unable to read correct number of titles");
      delete this->IS;
      this->IS = nullptr;
      return 0;
    }
    // sanitize the title name just in case of any trailing whitespace or quotes
    std::string title(line);
    this->SanitizeFileName(title);
    titles.emplace_back(title);
    this->EulerTransformsMap[title] = TimeToEulerTransMapType();
  }

  // rest of file is Time Step sections
  int lineRead = this->ReadNextDataLine(line);
  int timeIdx = 0;
  while (lineRead && timeIdx < numTimes)
  {
    if (strncmp(line, "Time Step:", 10) != 0)
    {
      vtkErrorMacro("The line " << line << " should be 'Time Step:'");
      delete this->IS;
      this->IS = nullptr;
      return 0;
    }

    this->ReadNextDataLine(line);
    double time = std::stod(line);
    if (this->UseEulerTimeSteps)
    {
      this->EulerTimeSteps->SetComponent(timeIdx, 0, time);
    }

    for (int transIdx = 0; transIdx < numTrans; ++transIdx)
    {
      const auto& title = titles[transIdx];
      if (this->EulerTransformsMap.count(title) == 0)
      {
        vtkErrorMacro("The EulerTransformsMap for title " << title << " could not be found");
        delete this->IS;
        this->IS = nullptr;
        return 0;
      }
      auto& titleMap = this->EulerTransformsMap[title];

      if (this->ReadNextDataLine(line) == 0)
      {
        vtkErrorMacro("Unable to read line containing euler parameters. got " << line);
        delete this->IS;
        this->IS = nullptr;
        return 0;
      }

      // each line should have 7 floats:
      // 3 translations in x, y, z and 4 euler parameters
      double tx, ty, tz, e0, e1, e2, e3;
      if (sscanf(line, " %lf %lf %lf %lf %lf %lf %lf", &tx, &ty, &tz, &e0, &e1, &e2, &e3) != 7)
      {
        vtkErrorMacro("Unable to read translation and euler parameters from line " << line);
      }

      vtkNew<vtkTransform> transform;
      transform->PostMultiply();
      vtkNew<vtkMatrix4x4> eulerRotation;
      eulerRotation->Identity();
      // see https://mathworld.wolfram.com/EulerParameters.html
      // for details. the elements in the matrix are eqns 18-26
      eulerRotation->SetElement(0, 0, e0 * e0 + e1 * e1 - e2 * e2 - e3 * e3);
      eulerRotation->SetElement(0, 1, 2 * (e1 * e2 + e0 * e3));
      eulerRotation->SetElement(0, 2, 2 * (e1 * e3 - e0 * e2));
      eulerRotation->SetElement(1, 0, 2 * (e1 * e2 - e0 * e3));
      eulerRotation->SetElement(1, 1, e0 * e0 - e1 * e1 + e2 * e2 - e3 * e3);
      eulerRotation->SetElement(1, 2, 2 * (e2 * e3 + e0 * e1));
      eulerRotation->SetElement(2, 0, 2 * (e1 * e3 + e0 * e2));
      eulerRotation->SetElement(2, 1, 2 * (e2 * e3 - e0 * e1));
      eulerRotation->SetElement(2, 2, e0 * e0 - e1 * e1 - e2 * e2 + e3 * e3);
      transform->Concatenate(eulerRotation);
      // translations should be done after the euler rotation
      transform->Translate(tx, ty, tz);

      titleMap[time] = transform;
    }

    lineRead = this->ReadNextDataLine(line);
    timeIdx++;
  }

  delete this->IS;
  this->IS = nullptr;
  return 1;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ApplyRigidBodyTransforms(int partId, const char* name, vtkDataSet* output)
{
  if (strcmp(this->GetClassName(), "vtkEnSightGoldReader") != 0 &&
    strcmp(this->GetClassName(), "vtkEnSightGoldBinaryReader") != 0)
  {
    vtkErrorMacro("Rigid Body files are only supported for EnSight Gold readers.");
  }

  std::string partName = name;
  if (!this->UsePartNamesRB)
  {
    // need to first convert part id to a string and use that as the partName
    partName = std::to_string(partId);
  }
  if (this->RigidBodyTransforms.find(partName) == this->RigidBodyTransforms.end())
  {
    return 1;
  }

  // first we need to concatenate pretransforms, euler transforms, and post transforms
  // We have to apply some transforms with TransformAllInputVectors on and some with it off.
  const auto& partTransforms = this->RigidBodyTransforms[partName];

  std::vector<vtkSmartPointer<vtkTransformFilter>> transformPipeline;
  // first check to see if we have any pretransforms
  for (unsigned int i = 0; i < partTransforms.PreTransforms.size(); i++)
  {
    transformPipeline.push_back(vtkSmartPointer<vtkTransformFilter>::New());
    vtkTransformFilter* filter = transformPipeline.back();

    if (i == 0)
    {
      filter->SetInputData(output);
    }
    else
    {
      filter->SetInputConnection(transformPipeline[i - 1]->GetOutputPort(0));
    }

    filter->SetTransform(partTransforms.PreTransforms[i]);

    if (partTransforms.PreTransformsApplyToVectors[i])
    {
      filter->TransformAllInputVectorsOn();
    }
  }

  // now find the correct euler transform
  auto eulerTitle = partTransforms.EETTransTitle;
  // need to make sure we don't have quotes or trailing whitespace even though it's not a filename
  this->SanitizeFileName(eulerTitle);
  if (this->EulerTransformsMap.find(eulerTitle) == this->EulerTransformsMap.end())
  {
    vtkErrorMacro("could not find '" << eulerTitle << "' in the EulerTransformsMap.");
    return 0;
  }

  auto& titleMap = this->EulerTransformsMap[eulerTitle];
  if (titleMap.find(this->ActualTimeValue) == titleMap.end())
  {
    vtkErrorMacro("could not find time step " << this->ActualTimeValue
                                              << " in the euler transformations map"
                                                 " for part '"
                                              << name << "' with title '" << eulerTitle << "'");
    return 0;
  }

  auto eulerTransform = this->EulerTransformsMap[eulerTitle][this->ActualTimeValue];
  transformPipeline.push_back(vtkSmartPointer<vtkTransformFilter>::New());
  vtkTransformFilter* filter = transformPipeline.back();
  if (transformPipeline.size() > 1)
  {
    filter->SetInputConnection(transformPipeline[transformPipeline.size() - 2]->GetOutputPort(0));
  }
  else
  {
    filter->SetInputData(output);
  }
  filter->SetTransform(eulerTransform);

  // now handle any post transforms
  for (unsigned int i = 0; i < partTransforms.PostTransforms.size(); i++)
  {
    // there's always at least 1 transform in the pipeline at this point
    auto prevTransFilter = transformPipeline.back();

    transformPipeline.push_back(vtkSmartPointer<vtkTransformFilter>::New());
    vtkTransformFilter* curFilter = transformPipeline.back();

    curFilter->SetInputConnection(prevTransFilter->GetOutputPort(0));
    curFilter->SetTransform(partTransforms.PostTransforms[i]);

    if (partTransforms.PostTransformsApplyToVectors[i])
    {
      curFilter->TransformAllInputVectorsOn();
    }
  }

  transformPipeline.back()->Update();
  output->ShallowCopy(transformPipeline.back()->GetOutput());
  return 1;
}

//------------------------------------------------------------------------------
int vtkEnSightReader::ReadVariableFiles(vtkMultiBlockDataSet* output)
{
  int i, j;
  char description[256];
  int timeSet, fileSet, timeStep, timeStepInFile, numSteps;
  vtkDataArray* times;
  float newTime;
  vtkIdList *numStepsList, *filenameNumbers;
  int fileNum;
  int validTime, filenameNum;
  char *fileName, *fileName2;

  for (i = 0; i < this->NumberOfVariables; i++)
  {
    switch (this->VariableTypes[i])
    {
      case SCALAR_PER_NODE:
      case VECTOR_PER_NODE:
      case TENSOR_ASYM_PER_NODE:
      case TENSOR_SYMM_PER_NODE:
      case SCALAR_PER_MEASURED_NODE:
      case VECTOR_PER_MEASURED_NODE:
        if (!this->GetPointArrayStatus(this->VariableDescriptions[i]))
        {
          continue;
        }
        break;
      case SCALAR_PER_ELEMENT:
      case VECTOR_PER_ELEMENT:
      case TENSOR_ASYM_PER_ELEMENT:
      case TENSOR_SYMM_PER_ELEMENT:
        if (!this->GetCellArrayStatus(this->VariableDescriptions[i]))
        {
          continue;
        }
        break;
    }

    timeStep = 0;
    timeStepInFile = 1;
    fileNum = 0;
    validTime = 1;
    fileName = new char[strlen(this->VariableFileNames[i]) + 10];
    strcpy(fileName, this->VariableFileNames[i]);
    if (this->UseTimeSets)
    {
      validTime = 0;
      timeSet = this->VariableTimeSetIds->GetId(i);
      times = this->TimeSets->GetItem(this->TimeSetIds->IsId(timeSet));
      for (j = 0; j < times->GetNumberOfTuples(); j++)
      {
        newTime = times->GetComponent(j, 0);
        if (newTime <= this->ActualTimeValue)
        {
          timeStep++;
          if (this->VariableTypes[i] == SCALAR_PER_MEASURED_NODE ||
            this->VariableTypes[i] == VECTOR_PER_MEASURED_NODE)
          {
            if (newTime >= this->MeasuredTimeValue || this->MeasuredTimeSet == -1)
            {
              validTime = 1;
            }
          }
          else if (newTime >= this->GeometryTimeValue || this->GeometryTimeSet == -1)
          {
            validTime = 1;
          }
        }
      }
      if (this->TimeSetFileNameNumbers->GetNumberOfItems() > 0 && validTime)
      {
        int collectionNum = this->TimeSetsWithFilenameNumbers->IsId(timeSet);
        if (collectionNum > -1)
        {
          filenameNumbers = this->TimeSetFileNameNumbers->GetItem(collectionNum);
          filenameNum = filenameNumbers->GetId(timeStep - 1);
          if (!this->UseFileSets)
          {
            vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName, filenameNum);
          }
        }
      }

      // There can only be file sets if there are also time sets.
      if (this->UseFileSets)
      {
        timeStepInFile = timeStep;
        fileSet = this->VariableFileSetIds->GetId(i);
        numStepsList = static_cast<vtkIdList*>(
          this->FileSetNumberOfSteps->GetItemAsObject(this->FileSets->IsId(fileSet)));

        if (numStepsList != nullptr && timeStep > numStepsList->GetId(0))
        {
          numSteps = numStepsList->GetId(0);
          timeStepInFile -= numSteps;
          fileNum = 1;
          for (j = 1; j < numStepsList->GetNumberOfIds(); j++)
          {
            int curNumSteps = numStepsList->GetId(j);
            numSteps += curNumSteps;
            if (timeStep > numSteps)
            {
              fileNum++;
              timeStepInFile -= curNumSteps;
            }
          }
        }
        if (this->FileSetFileNameNumbers->GetNumberOfItems() > 0 && validTime)
        {
          int collectionNum = this->FileSetsWithFilenameNumbers->IsId(fileSet);
          if (collectionNum > -1)
          {
            filenameNumbers = this->FileSetFileNameNumbers->GetItem(collectionNum);
            filenameNum = filenameNumbers->GetId(fileNum);
            vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName, filenameNum);
          }
        }
      }
    }

    if (validTime)
    {
      switch (this->VariableTypes[i])
      {
        case vtkEnSightReader::SCALAR_PER_NODE:
          this->ReadScalarsPerNode(fileName, this->VariableDescriptions[i], timeStepInFile, output);
          break;
        case vtkEnSightReader::SCALAR_PER_MEASURED_NODE:
          this->ReadScalarsPerNode(
            fileName, this->VariableDescriptions[i], timeStepInFile, output, 1);
          break;
        case vtkEnSightReader::VECTOR_PER_NODE:
          this->ReadVectorsPerNode(fileName, this->VariableDescriptions[i], timeStepInFile, output);
          break;
        case vtkEnSightReader::VECTOR_PER_MEASURED_NODE:
          this->ReadVectorsPerNode(
            fileName, this->VariableDescriptions[i], timeStepInFile, output, 1);
          break;
        case vtkEnSightReader::TENSOR_ASYM_PER_NODE:
          this->ReadAsymmetricTensorsPerNode(
            fileName, this->VariableDescriptions[i], timeStepInFile, output);
          break;
        case vtkEnSightReader::TENSOR_SYMM_PER_NODE:
          this->ReadTensorsPerNode(fileName, this->VariableDescriptions[i], timeStepInFile, output);
          break;
        case vtkEnSightReader::SCALAR_PER_ELEMENT:
          this->ReadScalarsPerElement(
            fileName, this->VariableDescriptions[i], timeStepInFile, output);
          break;
        case vtkEnSightReader::VECTOR_PER_ELEMENT:
          this->ReadVectorsPerElement(
            fileName, this->VariableDescriptions[i], timeStepInFile, output);
          break;
        case vtkEnSightReader::TENSOR_ASYM_PER_ELEMENT:
          this->ReadAsymmetricTensorsPerElement(
            fileName, this->VariableDescriptions[i], timeStepInFile, output);
          break;
        case vtkEnSightReader::TENSOR_SYMM_PER_ELEMENT:
          this->ReadTensorsPerElement(
            fileName, this->VariableDescriptions[i], timeStepInFile, output);
          break;
      }
    }
    delete[] fileName;
  }
  for (i = 0; i < this->NumberOfComplexVariables; i++)
  {
    switch (this->ComplexVariableTypes[i])
    {
      case COMPLEX_SCALAR_PER_NODE:
      case COMPLEX_VECTOR_PER_NODE:
        if (!this->GetPointArrayStatus(this->ComplexVariableDescriptions[i]))
        {
          continue;
        }
        break;
      case COMPLEX_SCALAR_PER_ELEMENT:
      case COMPLEX_VECTOR_PER_ELEMENT:
        if (!this->GetCellArrayStatus(this->ComplexVariableDescriptions[i]))
        {
          continue;
        }
        break;
    }
    timeStep = 0;
    timeStepInFile = 1;
    fileNum = 0;
    validTime = 1;
    fileName = new char[strlen(this->ComplexVariableFileNames[2 * i]) + 10];
    strcpy(fileName, this->ComplexVariableFileNames[2 * i]);
    fileName2 = new char[strlen(this->ComplexVariableFileNames[2 * i + 1]) + 10];
    strcpy(fileName2, this->ComplexVariableFileNames[2 * i + 1]);
    if (this->UseTimeSets)
    {
      validTime = 0;
      timeSet = this->VariableTimeSetIds->GetId(i);
      times = this->TimeSets->GetItem(this->TimeSetIds->IsId(timeSet));
      for (j = 0; j < times->GetNumberOfTuples(); j++)
      {
        newTime = times->GetComponent(j, 0);
        if (newTime <= this->ActualTimeValue)
        {
          timeStep++;
          if (this->VariableTypes[i] == SCALAR_PER_MEASURED_NODE ||
            this->VariableTypes[i] == VECTOR_PER_MEASURED_NODE)
          {
            if (newTime >= this->MeasuredTimeValue)
            {
              validTime = 1;
            }
          }
          else if (newTime >= this->GeometryTimeValue)
          {
            validTime = 1;
          }
        }
      }
      if (this->TimeSetFileNameNumbers->GetNumberOfItems() > 0 && validTime)
      {
        int collectionNum = this->TimeSetsWithFilenameNumbers->IsId(timeSet);
        if (collectionNum > -1)
        {
          filenameNumbers = this->TimeSetFileNameNumbers->GetItem(collectionNum);
          filenameNum = filenameNumbers->GetId(timeStep - 1);
          vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName, filenameNum);
          vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName2, filenameNum);
        }
      }

      // There can only be file sets if there are also time sets.
      if (this->UseFileSets)
      {
        timeStepInFile = timeStep;
        fileSet = this->VariableFileSetIds->GetId(i);
        numStepsList = static_cast<vtkIdList*>(
          this->FileSetNumberOfSteps->GetItemAsObject(this->FileSets->IsId(fileSet)));

        if (timeStep > numStepsList->GetId(0))
        {
          numSteps = numStepsList->GetId(0);
          timeStepInFile -= numSteps;
          fileNum = 1;
          for (j = 1; j < numStepsList->GetNumberOfIds(); j++)
          {
            int curNumSteps = numStepsList->GetId(j);
            numSteps += curNumSteps;
            if (timeStep > numSteps)
            {
              fileNum++;
              timeStepInFile -= curNumSteps;
            }
          }
        }
        if (this->FileSetFileNameNumbers->GetNumberOfItems() > 0 && validTime)
        {
          int collectionNum = this->FileSetsWithFilenameNumbers->IsId(fileSet);
          if (collectionNum > -1)
          {
            filenameNumbers = this->FileSetFileNameNumbers->GetItem(collectionNum);
            filenameNum = filenameNumbers->GetId(timeStep - 1);
            vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName, filenameNum);
            vtkGenericEnSightReader::ReplaceWildcardsHelper(fileName2, filenameNum);
          }
        }
      }
    }

    if (validTime)
    {
      switch (this->ComplexVariableTypes[i])
      {
        case vtkEnSightReader::COMPLEX_SCALAR_PER_NODE:
          this->ReadScalarsPerNode(
            fileName, this->ComplexVariableDescriptions[i], timeStepInFile, output, 0, 2);
          this->ReadScalarsPerNode(
            fileName2, this->ComplexVariableDescriptions[i], timeStepInFile, output, 0, 2, 1);
          break;
        case vtkEnSightReader::COMPLEX_VECTOR_PER_NODE:
          strcpy(description, this->ComplexVariableDescriptions[i]);
          strcat(description, "_r");
          this->ReadVectorsPerNode(fileName, description, timeStepInFile, output);
          strcpy(description, this->ComplexVariableDescriptions[i]);
          strcat(description, "_i");
          this->ReadVectorsPerNode(fileName2, description, timeStepInFile, output);
          break;
        case vtkEnSightReader::COMPLEX_SCALAR_PER_ELEMENT:
          this->ReadScalarsPerElement(
            fileName, this->ComplexVariableDescriptions[i], timeStepInFile, output, 2);
          this->ReadScalarsPerElement(
            fileName2, this->ComplexVariableDescriptions[i], timeStepInFile, output, 2, 1);
          break;
        case vtkEnSightReader::COMPLEX_VECTOR_PER_ELEMENT:
          strcpy(description, this->ComplexVariableDescriptions[i]);
          strcat(description, "_r");
          this->ReadVectorsPerElement(fileName, description, timeStepInFile, output);
          strcpy(description, this->ComplexVariableDescriptions[i]);
          strcat(description, "_i");
          this->ReadVectorsPerElement(fileName2, description, timeStepInFile, output);
          break;
      }
    }
    delete[] fileName;
    delete[] fileName2;
  }

  return 1;
}

//------------------------------------------------------------------------------
void vtkEnSightReader::AddVariableFileName(const char* fileName1, const char* fileName2)
{
  int size;
  int i;
  bool complexType = this->VariableMode == vtkEnSightReader::COMPLEX_SCALAR_PER_NODE ||
    this->VariableMode == vtkEnSightReader::COMPLEX_VECTOR_PER_NODE ||
    this->VariableMode == vtkEnSightReader::COMPLEX_SCALAR_PER_ELEMENT ||
    this->VariableMode == vtkEnSightReader::COMPLEX_VECTOR_PER_ELEMENT;

  if (!complexType)
  {
    size = this->NumberOfVariables;

    char** newFileNameList = new char*[size]; // temporary array

    // copy file names to temporary array
    for (i = 0; i < size; i++)
    {
      newFileNameList[i] = new char[strlen(this->VariableFileNames[i]) + 1];
      strcpy(newFileNameList[i], this->VariableFileNames[i]);
      delete[] this->VariableFileNames[i];
    }
    delete[] this->VariableFileNames;

    // make room for new file name
    this->VariableFileNames = new char*[size + 1];

    // copy existing file names back to first array
    for (i = 0; i < size; i++)
    {
      this->VariableFileNames[i] = new char[strlen(newFileNameList[i]) + 1];
      strcpy(this->VariableFileNames[i], newFileNameList[i]);
      delete[] newFileNameList[i];
    }
    delete[] newFileNameList;

    // add new file name at end of first array
    this->VariableFileNames[size] = new char[strlen(fileName1) + 1];
    strcpy(this->VariableFileNames[size], fileName1);
    vtkDebugMacro(<< "file name: " << this->VariableFileNames[size]);
  }
  else
  {
    size = this->NumberOfComplexVariables;

    char** newFileNameList = new char*[2 * size]; // temporary array

    // copy file names to temporary array
    for (i = 0; i < 2 * size; i++)
    {
      newFileNameList[i] = new char[strlen(this->ComplexVariableFileNames[i]) + 1];
      strcpy(newFileNameList[i], this->ComplexVariableFileNames[i]);
      delete[] this->ComplexVariableFileNames[i];
    }
    delete[] this->ComplexVariableFileNames;

    // make room for new file name
    this->ComplexVariableFileNames = new char*[2 * (size + 1)];

    // copy existing file names back to first array
    for (i = 0; i < 2 * size; i++)
    {
      this->ComplexVariableFileNames[i] = new char[strlen(newFileNameList[i]) + 1];
      strcpy(this->ComplexVariableFileNames[i], newFileNameList[i]);
      delete[] newFileNameList[i];
    }
    delete[] newFileNameList;

    // add new file name at end of first array
    this->ComplexVariableFileNames[2 * size] = new char[strlen(fileName1) + 1];
    strcpy(this->ComplexVariableFileNames[2 * size], fileName1);
    vtkDebugMacro("real file name: " << this->ComplexVariableFileNames[2 * size]);
    this->ComplexVariableFileNames[2 * size + 1] = new char[strlen(fileName2) + 1];
    strcpy(this->ComplexVariableFileNames[2 * size + 1], fileName2);
    vtkDebugMacro("imag. file name: " << this->ComplexVariableFileNames[2 * size + 1]);
  }
}

//------------------------------------------------------------------------------
void vtkEnSightReader::AddVariableDescription(const char* description)
{
  int size;
  int i;
  bool complexType = this->VariableMode == vtkEnSightReader::COMPLEX_SCALAR_PER_NODE ||
    this->VariableMode == vtkEnSightReader::COMPLEX_VECTOR_PER_NODE ||
    this->VariableMode == vtkEnSightReader::COMPLEX_SCALAR_PER_ELEMENT ||
    this->VariableMode == vtkEnSightReader::COMPLEX_VECTOR_PER_ELEMENT;

  if (!complexType)
  {
    size = this->NumberOfVariables;

    char** newDescriptionList = new char*[size]; // temporary array

    // copy descriptions to temporary array
    for (i = 0; i < size; i++)
    {
      newDescriptionList[i] = new char[strlen(this->VariableDescriptions[i]) + 1];
      strcpy(newDescriptionList[i], this->VariableDescriptions[i]);
      delete[] this->VariableDescriptions[i];
    }
    delete[] this->VariableDescriptions;

    // make room for new description
    this->VariableDescriptions = new char*[size + 1];

    // copy existing descriptions back to first array
    for (i = 0; i < size; i++)
    {
      this->VariableDescriptions[i] = new char[strlen(newDescriptionList[i]) + 1];
      strcpy(this->VariableDescriptions[i], newDescriptionList[i]);
      delete[] newDescriptionList[i];
    }
    delete[] newDescriptionList;

    // add new description at end of first array
    this->VariableDescriptions[size] = new char[strlen(description) + 1];
    strcpy(this->VariableDescriptions[size], description);
    vtkDebugMacro("description: " << this->VariableDescriptions[size]);
  }
  else
  {
    size = this->NumberOfComplexVariables;

    char** newDescriptionList = new char*[size]; // temporary array

    // copy descriptions to temporary array
    for (i = 0; i < size; i++)
    {
      newDescriptionList[i] = new char[strlen(this->ComplexVariableDescriptions[i]) + 1];
      strcpy(newDescriptionList[i], this->ComplexVariableDescriptions[i]);
      delete[] this->ComplexVariableDescriptions[i];
    }
    delete[] this->ComplexVariableDescriptions;

    // make room for new description
    this->ComplexVariableDescriptions = new char*[size + 1];

    // copy existing descriptions back to first array
    for (i = 0; i < size; i++)
    {
      this->ComplexVariableDescriptions[i] = new char[strlen(newDescriptionList[i]) + 1];
      strcpy(this->ComplexVariableDescriptions[i], newDescriptionList[i]);
      delete[] newDescriptionList[i];
    }
    delete[] newDescriptionList;

    // add new description at end of first array
    this->ComplexVariableDescriptions[size] = new char[strlen(description) + 1];
    strcpy(this->ComplexVariableDescriptions[size], description);
    vtkDebugMacro("description: " << this->ComplexVariableDescriptions[size]);
  }
}

//------------------------------------------------------------------------------
void vtkEnSightReader::AddVariableType()
{
  int size;
  int i;
  int* types = nullptr;
  bool complexType = this->VariableMode == vtkEnSightReader::COMPLEX_SCALAR_PER_NODE ||
    this->VariableMode == vtkEnSightReader::COMPLEX_VECTOR_PER_NODE ||
    this->VariableMode == vtkEnSightReader::COMPLEX_SCALAR_PER_ELEMENT ||
    this->VariableMode == vtkEnSightReader::COMPLEX_VECTOR_PER_ELEMENT;

  // Figure out what the size of the variable type array is
  if (!complexType)
  {
    size = this->NumberOfVariables;

    types = new int[size];

    for (i = 0; i < size; i++)
    {
      types[i] = this->VariableTypes[i];
    }
    delete[] this->VariableTypes;

    this->VariableTypes = new int[size + 1];
    for (i = 0; i < size; i++)
    {
      this->VariableTypes[i] = types[i];
    }
    delete[] types;
    this->VariableTypes[size] = this->VariableMode;
    vtkDebugMacro("variable type: " << this->VariableTypes[size]);
  }
  else
  {
    size = this->NumberOfComplexVariables;

    if (size > 0)
    {
      types = new int[size];
      for (i = 0; i < size; i++)
      {
        types[i] = this->ComplexVariableTypes[i];
      }
      delete[] this->ComplexVariableTypes;
    }

    this->ComplexVariableTypes = new int[size + 1];
    for (i = 0; i < size; i++)
    {
      this->ComplexVariableTypes[i] = types[i];
    }

    if (size > 0)
    {
      delete[] types;
    }
    this->ComplexVariableTypes[size] = this->VariableMode;
    vtkDebugMacro("complex variable type: " << this->ComplexVariableTypes[size]);
  }
}

//------------------------------------------------------------------------------
int vtkEnSightReader::GetSectionType(const char* line)
{
  if (strncmp(line, "coordinates", 5) == 0)
  {
    return vtkEnSightReader::COORDINATES;
  }
  else if (strncmp(line, "block", 4) == 0)
  {
    return vtkEnSightReader::BLOCK;
  }
  else if (this->GetElementType(line) != -1)
  {
    return vtkEnSightReader::ELEMENT;
  }
  else
  {
    return -1;
  }
}

//------------------------------------------------------------------------------
int vtkEnSightReader::GetElementType(const char* line)
{
  if (strncmp(line, "point", 5) == 0)
  {
    return vtkEnSightReader::POINT;
  }
  else if (strncmp(line, "bar2", 4) == 0)
  {
    return vtkEnSightReader::BAR2;
  }
  else if (strncmp(line, "bar3", 4) == 0)
  {
    return vtkEnSightReader::BAR3;
  }
  else if (strncmp(line, "nsided", 6) == 0)
  {
    return vtkEnSightReader::NSIDED;
  }
  else if (strncmp(line, "tria3", 5) == 0)
  {
    return vtkEnSightReader::TRIA3;
  }
  else if (strncmp(line, "tria6", 5) == 0)
  {
    return vtkEnSightReader::TRIA6;
  }
  else if (strncmp(line, "quad4", 5) == 0)
  {
    return vtkEnSightReader::QUAD4;
  }
  else if (strncmp(line, "quad8", 5) == 0)
  {
    return vtkEnSightReader::QUAD8;
  }
  else if (strncmp(line, "nfaced", 6) == 0)
  {
    return vtkEnSightReader::NFACED;
  }
  else if (strncmp(line, "tetra4", 6) == 0)
  {
    return vtkEnSightReader::TETRA4;
  }
  else if (strncmp(line, "tetra10", 7) == 0)
  {
    return vtkEnSightReader::TETRA10;
  }
  else if (strncmp(line, "pyramid5", 8) == 0)
  {
    return vtkEnSightReader::PYRAMID5;
  }
  else if (strncmp(line, "pyramid13", 9) == 0)
  {
    return vtkEnSightReader::PYRAMID13;
  }
  else if (strncmp(line, "hexa8", 5) == 0)
  {
    return vtkEnSightReader::HEXA8;
  }
  else if (strncmp(line, "hexa20", 6) == 0)
  {
    return vtkEnSightReader::HEXA20;
  }
  else if (strncmp(line, "penta6", 6) == 0)
  {
    return vtkEnSightReader::PENTA6;
  }
  else if (strncmp(line, "penta15", 7) == 0)
  {
    return vtkEnSightReader::PENTA15;
  }
  else
  {
    return -1;
  }
}

//------------------------------------------------------------------------------
void vtkEnSightReader::RemoveLeadingBlanks(char* line)
{
  int count = 0;
  while (line[count] == ' ')
  {
    count++;
  }
  memmove(line, line + count, strlen(line + count) + 1);
}

//------------------------------------------------------------------------------
vtkIdList* vtkEnSightReader::GetCellIds(int index, int cellType)
{
  // Check argument range.
  if (cellType < POINT || cellType >= NUMBER_OF_ELEMENT_TYPES)
  {
    vtkErrorMacro("Cell type " << cellType << " out of range.  Only " << NUMBER_OF_ELEMENT_TYPES - 1
                               << " types exist.");
    return nullptr;
  }
  if (index < 0 || index > this->UnstructuredPartIds->GetNumberOfIds())
  {
    vtkErrorMacro("Index " << index << " out of range.  Only "
                           << this->UnstructuredPartIds->GetNumberOfIds() << " IDs exist.");
    return nullptr;
  }

  // Create the container if necessary.
  if (!this->CellIds)
  {
    this->CellIds = new vtkEnSightReaderCellIdsType;
  }

  // Get the index of the actual vtkIdList requested.
  unsigned int cellIdsIndex = index * NUMBER_OF_ELEMENT_TYPES + cellType;

  // Make sure the container is large enough for this index.
  if (cellIdsIndex + 1 > this->CellIds->size())
  {
    this->CellIds->resize(cellIdsIndex + 1);
  }

  // Make sure this vtkIdList exists.
  if (!(*this->CellIds)[cellIdsIndex])
  {
    vtkIdList* nl = vtkIdList::New();
    (*this->CellIds)[cellIdsIndex] = nl;
    nl->Delete();
  }

  // Return the requested vtkIdList.
  return (*this->CellIds)[cellIdsIndex];
}

//------------------------------------------------------------------------------
void vtkEnSightReader::AddToBlock(
  vtkMultiBlockDataSet* output, unsigned int blockNo, vtkDataSet* dataset)
{
  vtkDataObject* blockDO = output->GetBlock(blockNo);
  if (blockDO)
  {
    vtkErrorMacro("Block already has a vtkDataSet assigned to it.");
    return;
  }

  output->SetBlock(blockNo, dataset);
}

//------------------------------------------------------------------------------
vtkDataSet* vtkEnSightReader::GetDataSetFromBlock(
  vtkMultiBlockDataSet* output, unsigned int blockno)
{
  return vtkDataSet::SafeDownCast(output->GetBlock(blockno));
}

//------------------------------------------------------------------------------
void vtkEnSightReader::SetBlockName(
  vtkMultiBlockDataSet* output, unsigned int blockNo, const char* name)
{
  output->GetMetaData(blockNo)->Set(vtkCompositeDataSet::NAME(), name);
}

//------------------------------------------------------------------------------
void vtkEnSightReader::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "CaseFileName: " << (this->CaseFileName ? this->CaseFileName : "(none)") << endl;
  os << indent << "FilePath: " << (this->FilePath ? this->FilePath : "(none)") << endl;
  os << indent << "NumberOfComplexScalarsPerNode: " << this->NumberOfComplexScalarsPerNode << endl;
  os << indent << "NumberOfVectorsPerElement :" << this->NumberOfVectorsPerElement << endl;
  os << indent << "NumberOfTensorsAsymPerElement: " << this->NumberOfTensorsAsymPerElement << endl;
  os << indent << "NumberOfTensorsSymmPerElement: " << this->NumberOfTensorsSymmPerElement << endl;
  os << indent << "NumberOfComplexVectorsPerNode: " << this->NumberOfComplexVectorsPerNode << endl;
  os << indent << "NumberOfScalarsPerElement: " << this->NumberOfScalarsPerElement << endl;
  os << indent << "NumberOfComplexVectorsPerElement: " << this->NumberOfComplexVectorsPerElement
     << endl;
  os << indent << "NumberOfComplexScalarsPerElement: " << this->NumberOfComplexScalarsPerElement
     << endl;
  os << indent << "NumberOfTensorsAsymPerNode: " << this->NumberOfTensorsAsymPerNode << endl;
  os << indent << "NumberOfTensorsSymmPerNode: " << this->NumberOfTensorsSymmPerNode << endl;
  os << indent << "NumberOfScalarsPerMeasuredNode: " << this->NumberOfScalarsPerMeasuredNode
     << endl;
  os << indent << "NumberOfVectorsPerMeasuredNode: " << this->NumberOfVectorsPerMeasuredNode
     << endl;
  os << indent << "NumberOfScalarsPerNode: " << this->NumberOfScalarsPerNode << endl;
  os << indent << "NumberOfVectorsPerNode: " << this->NumberOfVectorsPerNode << endl;
  os << indent << "TimeValue: " << this->TimeValue << endl;
  os << indent << "MinimumTimeValue: " << this->MinimumTimeValue << endl;
  os << indent << "MaximumTimeValue: " << this->MaximumTimeValue << endl;
  os << indent << "TimeSets: " << this->TimeSets << endl;
  os << indent
     << "MeasuredFileName: " << (this->MeasuredFileName ? this->MeasuredFileName : "(none)")
     << endl;
  os << indent << "MatchFileName: " << (this->MatchFileName ? this->MatchFileName : "(none)")
     << endl;
  os << indent
     << "RigidBodyFileName: " << (this->RigidBodyFileName ? this->RigidBodyFileName : "(none)")
     << endl;
  os << indent << "UsePartNamesRB: " << this->UsePartNamesRB << endl;
  os << indent << "UseEulerTimeSteps: " << this->UseEulerTimeSteps << endl;
  os << indent << "UseTimeSets: " << this->UseTimeSets << endl;
  os << indent << "UseFileSets: " << this->UseFileSets << endl;
}
VTK_ABI_NAMESPACE_END
