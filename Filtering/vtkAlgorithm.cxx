/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkAlgorithm.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkAlgorithm.h"

#include "vtkAlgorithmOutput.h"
#include "vtkCommand.h"
#include "vtkDataObject.h"
#include "vtkErrorCode.h"
#include "vtkGarbageCollector.h"
#include "vtkInformation.h"
#include "vtkInformationInformationVectorKey.h"
#include "vtkInformationIntegerKey.h"
#include "vtkInformationStringKey.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkSmartPointer.h"
#include "vtkStreamingDemandDrivenPipeline.h"

#include <vtkstd/set>
#include <vtkstd/vector>

vtkCxxRevisionMacro(vtkAlgorithm, "1.12");
vtkStandardNewMacro(vtkAlgorithm);

vtkCxxSetObjectMacro(vtkAlgorithm,Information,vtkInformation);

vtkInformationKeyMacro(vtkAlgorithm, INPUT_REQUIRED_DATA_TYPE, String);
vtkInformationKeyMacro(vtkAlgorithm, INPUT_IS_OPTIONAL, Integer);
vtkInformationKeyMacro(vtkAlgorithm, INPUT_IS_REPEATABLE, Integer);
vtkInformationKeyMacro(vtkAlgorithm, INPUT_REQUIRED_FIELDS, InformationVector);
vtkInformationKeyMacro(vtkAlgorithm, PORT_REQUIREMENTS_FILLED, Integer);

//----------------------------------------------------------------------------
class vtkAlgorithmInternals
{
public:
  // Proxy object instances for use in establishing connections from
  // the output ports to other algorithms.
  vtkstd::vector< vtkSmartPointer<vtkAlgorithmOutput> > Outputs;
};

//----------------------------------------------------------------------------
class vtkAlgorithmToExecutiveFriendship
{
public:
  static void SetAlgorithm(vtkExecutive* executive, vtkAlgorithm* algorithm)
    {
    executive->SetAlgorithm(algorithm);
    }
};

//----------------------------------------------------------------------------
vtkAlgorithm::vtkAlgorithm()
{
  this->AbortExecute = 0;
  this->ErrorCode = 0;
  this->Progress = 0.0;
  this->ProgressText = NULL;
  this->Executive = 0;
  this->InputPortInformation = vtkInformationVector::New();
  this->OutputPortInformation = vtkInformationVector::New();
  this->AlgorithmInternal = new vtkAlgorithmInternals;
  this->Information = vtkInformation::New();
  this->Information->Register(this);
  this->Information->Delete();
}

//----------------------------------------------------------------------------
vtkAlgorithm::~vtkAlgorithm()
{
  this->SetInformation(0);
  if(this->Executive)
    {
    this->Executive->UnRegister(this);
    this->Executive = 0;
    }
  this->InputPortInformation->Delete();
  this->OutputPortInformation->Delete();
  delete this->AlgorithmInternal;
  delete [] this->ProgressText;
  this->ProgressText = NULL;
}

// Update the progress of the process object. If a ProgressMethod exists,
// executes it. Then set the Progress ivar to amount. The parameter amount
// should range between (0,1).
void vtkAlgorithm::UpdateProgress(double amount)
{
  this->Progress = amount;
  this->InvokeEvent(vtkCommand::ProgressEvent,(void *)&amount);
}


//----------------------------------------------------------------------------
void vtkAlgorithm::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  if(this->HasExecutive())
    {
    os << indent << "Executive: " << this->Executive << "\n";
    }
  else
    {
    os << indent << "Executive: (none)\n";
    }
  
  os << indent << "ErrorCode: " << 
    vtkErrorCode::GetStringFromErrorCode(this->ErrorCode) << endl;
  
  if ( this->Information )
    {
    os << indent << "Information: " << this->Information << "\n";
    }
  else
    {
    os << indent << "Information: (none)\n";
    }

  os << indent << "AbortExecute: " << (this->AbortExecute ? "On\n" : "Off\n");
  os << indent << "Progress: " << this->Progress << "\n";
  if ( this->ProgressText )
    {
    os << indent << "Progress Text: " << this->ProgressText << "\n";
    }
  else
    {
    os << indent << "Progress Text: (None)\n";
    }
}

//----------------------------------------------------------------------------
int vtkAlgorithm::HasExecutive()
{
  return this->Executive? 1:0;
}

//----------------------------------------------------------------------------
vtkExecutive* vtkAlgorithm::GetExecutive()
{
  // Create the default executive if we do not have one already.
  if(!this->HasExecutive())
    {
    vtkExecutive* e = this->CreateDefaultExecutive();
    this->SetExecutive(e);
    e->Delete();
    }
  return this->Executive;
}

//----------------------------------------------------------------------------
void vtkAlgorithm::SetExecutive(vtkExecutive* newExecutive)
{
  vtkExecutive* oldExecutive = this->Executive;
  if(newExecutive != oldExecutive)
    {
    if(newExecutive)
      {
      newExecutive->Register(this);
      vtkAlgorithmToExecutiveFriendship::SetAlgorithm(newExecutive, this);
      }
    this->Executive = newExecutive;
    if(oldExecutive)
      {
      vtkAlgorithmToExecutiveFriendship::SetAlgorithm(oldExecutive, 0);
      oldExecutive->UnRegister(this);
      }
    }
}

//----------------------------------------------------------------------------
int vtkAlgorithm::ProcessRequest(vtkInformation*,
                                 vtkInformationVector**,
                                 vtkInformationVector*)
{
  return 1;
}

//----------------------------------------------------------------------------
int vtkAlgorithm::GetNumberOfInputPorts()
{
  return this->InputPortInformation->GetNumberOfInformationObjects();
}

//----------------------------------------------------------------------------
void vtkAlgorithm::SetNumberOfInputPorts(int n)
{
  // Sanity check.
  if(n < 0)
    {
    vtkErrorMacro("Attempt to set number of input ports to " << n);
    n = 0;
    }

  // We must remove all connections from ports that are removed.
  for(int i=n; i < this->GetNumberOfInputPorts(); ++i)
    {
    this->SetNumberOfInputConnections(i, 0);
    }

  // Set the number of input port information objects.
  this->InputPortInformation->SetNumberOfInformationObjects(n);
}

//----------------------------------------------------------------------------
int vtkAlgorithm::GetNumberOfOutputPorts()
{
  return this->OutputPortInformation->GetNumberOfInformationObjects();
}

//----------------------------------------------------------------------------
void vtkAlgorithm::SetNumberOfOutputPorts(int n)
{
  // Sanity check.
  if(n < 0)
    {
    vtkErrorMacro("Attempt to set number of output ports to " << n);
    n = 0;
    }

  // We must remove all connections from ports that are removed.
  for(int i=n; i < this->GetNumberOfOutputPorts(); ++i)
    {
    // Get the producer and its output information for this port.
    vtkExecutive* producer = this->GetExecutive();
    vtkInformation* info = producer->GetOutputInformation(i);

    // Remove all consumers' references to this producer on this port.
    vtkExecutive** consumers = info->GetExecutives(vtkExecutive::CONSUMERS());
    int* consumerPorts = info->GetPorts(vtkExecutive::CONSUMERS());
    int consumerCount = info->Length(vtkExecutive::CONSUMERS());
    for(int i=0; i < consumerCount; ++i)
      {
      vtkInformationVector* inputs =
        consumers[i]->GetInputInformation(consumerPorts[i]);
      inputs->Remove(info);
      }

    // Remove this producer's references to all consumers on this port.
    info->Remove(vtkExecutive::CONSUMERS());
    }

  // Set the number of output port information objects.
  this->OutputPortInformation->SetNumberOfInformationObjects(n);

  // Set the number of connection proxy objects.
  this->AlgorithmInternal->Outputs.resize(n);
}

//----------------------------------------------------------------------------
vtkDataObject* vtkAlgorithm::GetOutputDataObject(int port)
{
  if(!this->OutputPortIndexInRange(port, "get the data object for"))
    {
    return 0;
    }
  return this->GetExecutive()->GetOutputData(port);
}

//----------------------------------------------------------------------------
void vtkAlgorithm::RemoveAllInputs()
{
  this->SetInputConnection(0, 0);
}

//----------------------------------------------------------------------------
void vtkAlgorithm::SetInputConnection(int port, vtkAlgorithmOutput* input)
{
  if(!this->InputPortIndexInRange(port, "connect"))
    {
    return;
    }

  // Get the producer/consumer pair for the connection.
  vtkExecutive* producer =
    (input && input->GetProducer())? input->GetProducer()->GetExecutive() : 0;
  int producerPort = producer? input->GetIndex() : 0;
  vtkExecutive* consumer = this->GetExecutive();
  int consumerPort = port;

  // Get the vector of connected input information objects.
  vtkInformationVector* inputs = consumer->GetInputInformation(consumerPort);

  // Get the information object from the producer of the new input.
  vtkInformation* newInfo =
    producer? producer->GetOutputInformation(producerPort) : 0;

  // Check if the connection is already present.
  if(!newInfo && inputs->GetNumberOfInformationObjects() == 0)
    {
    return;
    }
  else if(newInfo == inputs->GetInformationObject(0) &&
          inputs->GetNumberOfInformationObjects() == 1)
    {
    return;
    }

  // The connection is not present.
  vtkDebugMacro("Setting connection to input port index " << consumerPort
                << " from output port index " << producerPort
                << " on algorithm "
                << (producer? producer->GetAlgorithm()->GetClassName() : "")
                << "(" << (producer? producer->GetAlgorithm() : 0) << ").");

  // Add this consumer to the new input's list of consumers.
  if(newInfo)
    {
    newInfo->Append(vtkExecutive::CONSUMERS(), consumer, consumerPort);
    }

  // Remove this consumer from all old inputs' lists of consumers.
  for(int i=0; i < inputs->GetNumberOfInformationObjects(); ++i)
    {
    if(vtkInformation* oldInfo = inputs->GetInformationObject(i))
      {
      oldInfo->Remove(vtkExecutive::CONSUMERS(), consumer, consumerPort);
      }
    }

  // Make the new input the only connection.
  if(newInfo)
    {
    inputs->SetInformationObject(0, newInfo);
    inputs->SetNumberOfInformationObjects(1);
    }
  else
    {
    inputs->SetNumberOfInformationObjects(0);
    }

  // This algorithm has been modified.
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkAlgorithm::AddInputConnection(int port, vtkAlgorithmOutput* input)
{
  if(!this->InputPortIndexInRange(port, "connect"))
    {
    return;
    }

  // If there is no input do nothing.
  if(!input || !input->GetProducer())
    {
    return;
    }

  // Get the producer/consumer pair for the connection.
  vtkExecutive* producer = input->GetProducer()->GetExecutive();
  int producerPort = input->GetIndex();
  vtkExecutive* consumer = this->GetExecutive();
  int consumerPort = port;

  // Get the vector of connected input information objects.
  vtkInformationVector* inputs = consumer->GetInputInformation(consumerPort);

  // Add the new connection.
  vtkDebugMacro("Adding connection to input port index " << consumerPort
                << " from output port index " << producerPort
                << " on algorithm "
                << (producer? producer->GetAlgorithm()->GetClassName() : "")
                << "(" << (producer? producer->GetAlgorithm() : 0) << ").");

  // Get the information object from the producer of the new input.
  vtkInformation* newInfo = producer->GetOutputInformation(producerPort);

  // Add this consumer to the input's list of consumers.
  newInfo->Append(vtkExecutive::CONSUMERS(), consumer, consumerPort);

  // Add the information object to the list of inputs.
  inputs->Append(newInfo);

  // This algorithm has been modified.
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkAlgorithm::RemoveInputConnection(int port, vtkAlgorithmOutput* input)
{
  if(!this->InputPortIndexInRange(port, "disconnect"))
    {
    return;
    }

  // If there is no input do nothing.
  if(!input || !input->GetProducer())
    {
    return;
    }

  // Get the producer/consumer pair for the connection.
  vtkExecutive* producer = input->GetProducer()->GetExecutive();
  int producerPort = input->GetIndex();
  vtkExecutive* consumer = this->GetExecutive();
  int consumerPort = port;

  // Get the vector of connected input information objects.
  vtkInformationVector* inputs = consumer->GetInputInformation(consumerPort);

  // Remove the connection.
  vtkDebugMacro("Removing connection to input port index " << consumerPort
                << " from output port index " << producerPort
                << " on algorithm "
                << (producer? producer->GetAlgorithm()->GetClassName() : "")
                << "(" << (producer? producer->GetAlgorithm() : 0) << ").");

  // Get the information object from the producer of the old input.
  vtkInformation* oldInfo = producer->GetOutputInformation(producerPort);

  // Remove this consumer from the old input's list of consumers.
  oldInfo->Remove(vtkExecutive::CONSUMERS(), consumer, consumerPort);

  // Remove the information object from the list of inputs.
  inputs->Remove(oldInfo);

  // This algorithm has been modified.
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkAlgorithm::SetNthInputConnection(int port, int index,
                                         vtkAlgorithmOutput* input)
{
  if(!this->InputPortIndexInRange(port, "replace connection"))
    {
    return;
    }

  // Get the producer/consumer pair for the connection.
  vtkExecutive* producer =
    (input && input->GetProducer())? input->GetProducer()->GetExecutive() : 0;
  int producerPort = producer? input->GetIndex() : 0;
  vtkExecutive* consumer = this->GetExecutive();
  int consumerPort = port;

  // Get the vector of connected input information objects.
  vtkInformationVector* inputs = consumer->GetInputInformation(consumerPort);

  // Check for any existing connection with this index.
  vtkInformation* oldInfo = inputs->GetInformationObject(index);

  // Get the information object from the producer of the input.
  vtkInformation* newInfo =
    producer? producer->GetOutputInformation(producerPort) : 0;

  // If the connection has not changed, do nothing.
  if(newInfo == oldInfo)
    {
    return;
    }

  // Set the connection.
  vtkDebugMacro("Setting connection index " << index
                << " to input port index " << consumerPort
                << " from output port index " << producerPort
                << " on algorithm "
                << (producer? producer->GetAlgorithm()->GetClassName() : "")
                << "(" << (producer? producer->GetAlgorithm() : 0) << ").");

  // Add the consumer to the new input's list of consumers.
  if(newInfo)
    {
    newInfo->Append(vtkExecutive::CONSUMERS(), consumer, consumerPort);
    }

  // Remove the consumer from the old input's list of consumers.
  if(oldInfo)
    {
    oldInfo->Remove(vtkExecutive::CONSUMERS(), consumer, consumerPort);
    }

  // Store the information object in the vector of input connections.
  inputs->SetInformationObject(index, newInfo);

  // This algorithm has been modified.
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkAlgorithm::SetNumberOfInputConnections(int port, int n)
{
  // Get the consumer executive and port number.
  vtkExecutive* consumer = this->GetExecutive();
  int consumerPort = port;

  // Get the vector of connected input information objects.
  vtkInformationVector* inputs = consumer->GetInputInformation(consumerPort);

  // If the number of connections has not changed, do nothing.
  if(n == inputs->GetNumberOfInformationObjects())
    {
    return;
    }

  // Remove connections beyond the new number.
  for(int i=n; i < inputs->GetNumberOfInformationObjects(); ++i)
    {
    // Remove each input's reference to this consumer.
    if(vtkInformation* oldInfo = inputs->GetInformationObject(i))
      {
      oldInfo->Remove(vtkExecutive::CONSUMERS(), consumer, consumerPort);
      }
    }

  // Set the number of connected inputs.  Non-existing inputs will be
  // empty information objects.
  inputs->SetNumberOfInformationObjects(n);

  // This algorithm has been modified.
  this->Modified();
}

//----------------------------------------------------------------------------
vtkAlgorithmOutput* vtkAlgorithm::GetOutputPort(int port)
{
  if(!this->OutputPortIndexInRange(port, "get"))
    {
    return 0;
    }

  // Create the vtkAlgorithmOutput proxy object if there is not one.
  if(!this->AlgorithmInternal->Outputs[port])
    {
    this->AlgorithmInternal->Outputs[port] =
      vtkSmartPointer<vtkAlgorithmOutput>::New();
    this->AlgorithmInternal->Outputs[port]->SetProducer(this);
    this->AlgorithmInternal->Outputs[port]->SetIndex(port);
    }

  // Return the proxy object instance.
  return this->AlgorithmInternal->Outputs[port];
}

//----------------------------------------------------------------------------
vtkInformation* vtkAlgorithm::GetInputPortInformation(int port)
{
  if(!this->InputPortIndexInRange(port, "get information object for"))
    {
    return 0;
    }

  // Get the input port information object.
  vtkInformation* info =
    this->InputPortInformation->GetInformationObject(port);

  // Fill it if it has not yet been filled.
  if(!info->Has(PORT_REQUIREMENTS_FILLED()))
    {
    if(this->FillInputPortInformation(port, info))
      {
      info->Set(PORT_REQUIREMENTS_FILLED(), 1);
      }
    else
      {
      info->Clear();
      }
    }

  // Return ths information object.
  return info;
}

//----------------------------------------------------------------------------
vtkInformation* vtkAlgorithm::GetOutputPortInformation(int port)
{
  if(!this->OutputPortIndexInRange(port, "get information object for"))
    {
    return 0;
    }

  // Get the output port information object.
  vtkInformation* info =
    this->OutputPortInformation->GetInformationObject(port);

  // Fill it if it has not yet been filled.
  if(!info->Has(PORT_REQUIREMENTS_FILLED()))
    {
    if(this->FillOutputPortInformation(port, info))
      {
      info->Set(PORT_REQUIREMENTS_FILLED(), 1);
      }
    else
      {
      info->Clear();
      }
    }

  // Return ths information object.
  return info;
}

//----------------------------------------------------------------------------
int vtkAlgorithm::FillInputPortInformation(int, vtkInformation*)
{
  vtkErrorMacro("FillInputPortInformation is not implemented.");
  return 0;
}

//----------------------------------------------------------------------------
int vtkAlgorithm::FillOutputPortInformation(int, vtkInformation*)
{
  vtkErrorMacro("FillOutputPortInformation is not implemented.");
  return 0;
}

//----------------------------------------------------------------------------
int vtkAlgorithm::GetNumberOfInputConnections(int port)
{
  if(this->Executive)
    {
    return this->Executive->GetNumberOfInputConnections(port);
    }
  return 0;
}

//----------------------------------------------------------------------------
int vtkAlgorithm::GetTotalNumberOfInputConnections()
{
  int i;
  int total = 0;
  for (i = 0; i < this->GetNumberOfInputPorts(); ++i)
    {
    total += this->GetNumberOfInputConnections(i);
    }
  return total;
}

//----------------------------------------------------------------------------
vtkAlgorithmOutput* vtkAlgorithm::GetInputConnection(int port, int index)
{
  if(!this->InputPortIndexInRange(port, "get a connection for"))
    {
    return 0;
    }
  if(index < 0 || index >= this->GetNumberOfInputConnections(port))
    {
    vtkErrorMacro("Attempt to get connection index " << index
                  << " for input port " << port << ", which has "
                  << this->GetNumberOfInputConnections(port)
                  << " connections.");
    return 0;
    }
  if(vtkInformation* info =
     this->GetExecutive()->GetInputInformation(port, index))
    {
    // Get the executive producing this input.  If there is none, then
    // it is a NULL input.
    vtkExecutive* producer = info->GetExecutive(vtkExecutive::PRODUCER());
    int producerPort = info->GetPort(vtkExecutive::PRODUCER());
    if(producer)
      {
      return producer->GetAlgorithm()->GetOutputPort(producerPort);
      }
    }
  return 0;
}

//----------------------------------------------------------------------------
int vtkAlgorithm::InputPortIndexInRange(int index, const char* action)
{
  // Make sure the index of the input port is in range.
  if(index < 0 || index >= this->GetNumberOfInputPorts())
    {
    vtkErrorMacro("Attempt to " << (action?action:"access")
                  << " input port index " << index
                  << " for an algorithm with "
                  << this->GetNumberOfInputPorts() << " input ports.");
    return 0;
    }
  return 1;
}

//----------------------------------------------------------------------------
int vtkAlgorithm::OutputPortIndexInRange(int index, const char* action)
{
  // Make sure the index of the output port is in range.
  if(index < 0 || index >= this->GetNumberOfOutputPorts())
    {
    vtkErrorMacro("Attempt to " << (action?action:"access")
                  << " output port index " << index
                  << " for an algorithm with "
                  << this->GetNumberOfOutputPorts() << " output ports.");
    return 0;
    }
  return 1;
}

//----------------------------------------------------------------------------
void vtkAlgorithm::Update()
{
  this->GetExecutive()->Update();
}

//----------------------------------------------------------------------------
void vtkAlgorithm::UpdateInformation()
{
  vtkDemandDrivenPipeline* ddp =
    vtkDemandDrivenPipeline::SafeDownCast(this->GetExecutive());
  if (ddp)
    {
    ddp->UpdateInformation();
    }
}

//----------------------------------------------------------------------------
void vtkAlgorithm::UpdateWholeExtent()
{
  vtkStreamingDemandDrivenPipeline* sddp =
    vtkStreamingDemandDrivenPipeline::SafeDownCast(this->GetExecutive());
  if (sddp)
    {
    sddp->UpdateWholeExtent();
    }
  else
    {
    this->Update();
    }
}

//----------------------------------------------------------------------------
vtkExecutive* vtkAlgorithm::CreateDefaultExecutive()
{
  return vtkStreamingDemandDrivenPipeline::New();
}

//----------------------------------------------------------------------------
void vtkAlgorithm::Register(vtkObjectBase* o)
{
  this->RegisterInternal(o, 1);
}

//----------------------------------------------------------------------------
void vtkAlgorithm::UnRegister(vtkObjectBase* o)
{
  this->UnRegisterInternal(o, 1);
}

//----------------------------------------------------------------------------
void vtkAlgorithm::ReportReferences(vtkGarbageCollector* collector)
{
  this->Superclass::ReportReferences(collector);
  vtkGarbageCollectorReport(collector, this->Executive, "Executive");
}

//----------------------------------------------------------------------------
void vtkAlgorithm::ConvertTotalInputToPortConnection(
  int ind, int &port, int &conn)
{
  port = 0;
  conn = 0;
  while (ind && port < this->GetNumberOfInputPorts())
    {
    int pNumCon;
    pNumCon = this->GetNumberOfInputConnections(port);
    if (ind >= pNumCon)
      {
      port++;
      ind -= pNumCon;
      }
    else
      {
      return;
      }
    }
}

//----------------------------------------------------------------------------
void vtkAlgorithm::ReleaseDataFlagOn()
{
  if(vtkDemandDrivenPipeline* ddp =
     vtkDemandDrivenPipeline::SafeDownCast(this->GetExecutive()))
    {
    for(int i=0; i < this->GetNumberOfOutputPorts(); ++i)
      {
      ddp->SetReleaseDataFlag(i, 1);
      }
    }
}

//----------------------------------------------------------------------------
void vtkAlgorithm::ReleaseDataFlagOff()
{
  if(vtkDemandDrivenPipeline* ddp =
     vtkDemandDrivenPipeline::SafeDownCast(this->GetExecutive()))
    {
    for(int i=0; i < this->GetNumberOfOutputPorts(); ++i)
      {
      ddp->SetReleaseDataFlag(i, 0);
      }
    }
}

//----------------------------------------------------------------------------
void vtkAlgorithm::SetReleaseDataFlag(int val)
{
  if(vtkDemandDrivenPipeline* ddp =
     vtkDemandDrivenPipeline::SafeDownCast(this->GetExecutive()))
    {
    for(int i=0; i < this->GetNumberOfOutputPorts(); ++i)
      {
      ddp->SetReleaseDataFlag(i, val);
      }
    }
}

//----------------------------------------------------------------------------
int vtkAlgorithm::GetReleaseDataFlag()
{
  if(vtkDemandDrivenPipeline* ddp =
     vtkDemandDrivenPipeline::SafeDownCast(this->GetExecutive()))
    {
    return ddp->GetReleaseDataFlag(0);
    }
  return 0;
}
