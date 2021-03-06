/*===================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center,
Division of Medical and Biological Informatics.
All rights reserved.

This software is distributed WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.

See LICENSE.txt or http://www.mitk.org for details.

===================================================================*/

#include "mitkSemanticRelationsIntegration.h"

// semantic relations module
#include "mitkControlPointManager.h"
#include "mitkDICOMHelper.h"
#include "mitkNodePredicates.h"
#include "mitkRelationStorage.h"
#include "mitkSemanticRelationException.h"
#include "mitkSemanticRelationsInference.h"
#include "mitkUIDGeneratorBoost.h"

// multi label module
#include <mitkLabelSetImage.h>

// c++
#include <iterator>
#include <algorithm>

std::vector<mitk::ISemanticRelationsObserver*> mitk::SemanticRelationsIntegration::m_ObserverVector;

void mitk::SemanticRelationsIntegration::AddObserver(ISemanticRelationsObserver* observer)
{
  std::vector<ISemanticRelationsObserver*>::iterator existingObserver = std::find(m_ObserverVector.begin(), m_ObserverVector.end(), observer);
  if (existingObserver != m_ObserverVector.end())
  {
    // no need to add the already existing observer
    return;
  }

  m_ObserverVector.push_back(observer);
}

void mitk::SemanticRelationsIntegration::RemoveObserver(ISemanticRelationsObserver* observer)
{
  m_ObserverVector.erase(std::remove(m_ObserverVector.begin(), m_ObserverVector.end(), observer), m_ObserverVector.end());
}

/************************************************************************/
/* functions to add / remove instances / attributes                     */
/************************************************************************/

void mitk::SemanticRelationsIntegration::AddImage(const DataNode* imageNode)
{
  if (nullptr == imageNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid image data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(imageNode);
  SemanticTypes::ID nodeID = GetIDFromDataNode(imageNode);

  RelationStorage::AddCase(caseID);
  RelationStorage::AddImage(caseID, nodeID);

  SemanticTypes::InformationType informationType = GetDICOMModalityFromDataNode(imageNode);
  AddInformationTypeToImage(imageNode, informationType);

  // set the correct control point for this image
  SemanticTypes::ControlPoint controlPoint = GenerateControlPoint(imageNode);
  SetControlPointOfImage(imageNode, controlPoint);
}

void mitk::SemanticRelationsIntegration::RemoveImage(const DataNode* imageNode)
{
  if (nullptr == imageNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid image data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(imageNode);
  SemanticTypes::ID nodeID = GetIDFromDataNode(imageNode);

  RemoveInformationTypeFromImage(imageNode);
  UnlinkImageFromControlPoint(imageNode);
  RelationStorage::RemoveImage(caseID, nodeID);
  NotifyObserver(caseID);
}

void mitk::SemanticRelationsIntegration::AddLesion(const SemanticTypes::CaseID& caseID, const SemanticTypes::Lesion& lesion)
{
  if (SemanticRelationsInference::InstanceExists(caseID, lesion))
  {
    mitkThrowException(SemanticRelationException) << "The lesion " << lesion.UID << " to add already exists for the given case.";
  }
  else
  {
    RelationStorage::AddLesion(caseID, lesion);
    NotifyObserver(caseID);
  }
}

void mitk::SemanticRelationsIntegration::OverwriteLesion(const SemanticTypes::CaseID& caseID, const SemanticTypes::Lesion& lesion)
{
  if (SemanticRelationsInference::InstanceExists(caseID, lesion))
  {
    RelationStorage::OverwriteLesion(caseID, lesion);
    NotifyObserver(caseID);
  }
  else
  {
    mitkThrowException(SemanticRelationException) << "The lesion " << lesion.UID << " to overwrite does not exist for the given case.";
  }
}

void mitk::SemanticRelationsIntegration::AddLesionAndLinkSegmentation(const DataNode* segmentationNode, const SemanticTypes::Lesion& lesion)
{
  if (nullptr == segmentationNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid segmentation data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(segmentationNode);
  AddLesion(caseID, lesion);
  LinkSegmentationToLesion(segmentationNode, lesion);
  NotifyObserver(caseID);
}

void mitk::SemanticRelationsIntegration::RemoveLesion(const SemanticTypes::CaseID& caseID, const SemanticTypes::Lesion& lesion)
{
  if (SemanticRelationsInference::InstanceExists(caseID, lesion))
  {
    SemanticTypes::IDVector allSegmentationIDsOfLesion = RelationStorage::GetAllSegmentationIDsOfLesion(caseID, lesion);
    if (allSegmentationIDsOfLesion.empty())
    {
      // no more segmentations are linked to the specific lesion
      // the lesion can be removed from the storage
      RelationStorage::RemoveLesion(caseID, lesion);
      NotifyObserver(caseID);
    }
    else
    {
      mitkThrowException(SemanticRelationException) << "The lesion " << lesion.UID << " to remove is still referred to by a segmentation node. Lesion will not be removed.";
    }
  }
  else
  {
    mitkThrowException(SemanticRelationException) << "The lesion " << lesion.UID << " to remove does not exist for the given case.";
  }
}

void mitk::SemanticRelationsIntegration::AddSegmentation(const DataNode* segmentationNode, const DataNode* parentNode)
{
  if (nullptr == segmentationNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid segmentation data node.";
  }

  if (nullptr == parentNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid parent data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(segmentationNode);
  SemanticTypes::ID segmentationNodeID = GetIDFromDataNode(segmentationNode);
  SemanticTypes::ID parentNodeID = GetIDFromDataNode(parentNode);

  RelationStorage::AddSegmentation(caseID, segmentationNodeID, parentNodeID);
  NotifyObserver(caseID);
}

void mitk::SemanticRelationsIntegration::LinkSegmentationToLesion(const DataNode* segmentationNode, const SemanticTypes::Lesion& lesion)
{
  if (nullptr == segmentationNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid segmentation data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(segmentationNode);
  if (SemanticRelationsInference::InstanceExists(caseID, lesion))
  {
    SemanticTypes::ID segmentationID = GetIDFromDataNode(segmentationNode);
    RelationStorage::LinkSegmentationToLesion(caseID, segmentationID, lesion);
    NotifyObserver(caseID);
  }
  else
  {
    mitkThrowException(SemanticRelationException) << "The lesion " << lesion.UID << " to link does not exist for the given case.";
  }
}

void mitk::SemanticRelationsIntegration::UnlinkSegmentationFromLesion(const DataNode* segmentationNode)
{
  if (nullptr == segmentationNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid segmentation data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(segmentationNode);
  SemanticTypes::ID segmentationID = GetIDFromDataNode(segmentationNode);
  RelationStorage::UnlinkSegmentationFromLesion(caseID, segmentationID);
  NotifyObserver(caseID);
}

void mitk::SemanticRelationsIntegration::RemoveSegmentation(const DataNode* segmentationNode)
{
  if (nullptr == segmentationNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid segmentation data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(segmentationNode);
  SemanticTypes::ID segmentationNodeID = GetIDFromDataNode(segmentationNode);
  RelationStorage::RemoveSegmentation(caseID, segmentationNodeID);
  NotifyObserver(caseID);
}

void mitk::SemanticRelationsIntegration::SetControlPointOfImage(const DataNode* imageNode, const SemanticTypes::ControlPoint& controlPoint)
{
  if (nullptr == imageNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid image data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(imageNode);
  SemanticTypes::ControlPointVector allControlPoints = RelationStorage::GetAllControlPointsOfCase(caseID);
  // need to check if an already existing control point fits/contains the user control point
  SemanticTypes::ControlPoint existingControlPoint = FindExistingControlPoint(controlPoint, allControlPoints);
  if (!existingControlPoint.UID.empty())
  {
    try
    {
      // found an already existing control point
      LinkImageToControlPoint(imageNode, existingControlPoint, false);
    }
    catch (const SemanticRelationException&)
    {
      mitkThrowException(SemanticRelationException) << "The image data node can not be linked. Inconsistency in the semantic relations storage assumed.";
    }
  }
  else
  {
    try
    {
      AddControlPointAndLinkImage(imageNode, controlPoint, false);
      // added a new control point
      // find closest control point to add the new control point to the correct examination period
      SemanticTypes::ControlPoint closestControlPoint = FindClosestControlPoint(controlPoint, allControlPoints);
      SemanticTypes::ExaminationPeriodVector allExaminationPeriods = RelationStorage::GetAllExaminationPeriodsOfCase(caseID);
      SemanticTypes::ExaminationPeriod examinationPeriod = FindExaminationPeriod(closestControlPoint, allExaminationPeriods);
      if (examinationPeriod.UID.empty())
      {
        // no closest control point (exceed threshold) or no examination period found
        // create a new examination period for this control point and add it to the storage
        examinationPeriod.UID = UIDGeneratorBoost::GenerateUID();
        examinationPeriod.name = "New examination period " + std::to_string(allExaminationPeriods.size());
        AddExaminationPeriod(caseID, examinationPeriod);
      }

      // add the control point to the (newly created or found / close) examination period
      AddControlPointToExaminationPeriod(caseID, controlPoint, examinationPeriod);
    }
    catch (const SemanticRelationException&)
    {
      mitkThrowException(SemanticRelationException) << "The image data node can not be linked. Inconsistency in the semantic relations storage assumed.";
    }
  }

  ClearControlPoints(caseID);
  NotifyObserver(caseID);
}

void mitk::SemanticRelationsIntegration::AddControlPointAndLinkImage(const DataNode* imageNode, const SemanticTypes::ControlPoint& controlPoint, bool checkConsistence)
{
  if (nullptr == imageNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid image data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(imageNode);
  if (SemanticRelationsInference::InstanceExists(caseID, controlPoint))
  {
    mitkThrowException(SemanticRelationException) << "The control point " << controlPoint.UID << " to add already exists for the given case. \n Use 'LinkImageToControlPoint' instead.";
  }

  RelationStorage::AddControlPoint(caseID, controlPoint);
  LinkImageToControlPoint(imageNode, controlPoint, checkConsistence);
}

void mitk::SemanticRelationsIntegration::LinkImageToControlPoint(const DataNode* imageNode, const SemanticTypes::ControlPoint& controlPoint, bool /*checkConsistence*/)
{
  if (nullptr == imageNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid image data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(imageNode);
  if (SemanticRelationsInference::InstanceExists(caseID, controlPoint))
  {
    SemanticTypes::ID dataID = GetIDFromDataNode(imageNode);
    RelationStorage::LinkImageToControlPoint(caseID, dataID, controlPoint);
  }
  else
  {
    mitkThrowException(SemanticRelationException) << "The control point " << controlPoint.UID << " to link does not exist for the given case.";
  }
}

void mitk::SemanticRelationsIntegration::UnlinkImageFromControlPoint(const DataNode* imageNode)
{
  if (nullptr == imageNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid image data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(imageNode);
  SemanticTypes::ID dataID = GetIDFromDataNode(imageNode);
  SemanticTypes::ControlPoint controlPoint = RelationStorage::GetControlPointOfImage(caseID, dataID);
  RelationStorage::UnlinkImageFromControlPoint(caseID, dataID);
  ClearControlPoints(caseID);
}

void mitk::SemanticRelationsIntegration::AddExaminationPeriod(const SemanticTypes::CaseID& caseID, const SemanticTypes::ExaminationPeriod& examinationPeriod)
{
  if (SemanticRelationsInference::InstanceExists(caseID, examinationPeriod))
  {
    mitkThrowException(SemanticRelationException) << "The examination period " << examinationPeriod.UID << " to add already exists for the given case.";
  }
  else
  {
    RelationStorage::AddExaminationPeriod(caseID, examinationPeriod);
  }
}

void mitk::SemanticRelationsIntegration::AddControlPointToExaminationPeriod(const SemanticTypes::CaseID& caseID, const SemanticTypes::ControlPoint& controlPoint, const SemanticTypes::ExaminationPeriod& examinationPeriod)
{
  if (!SemanticRelationsInference::InstanceExists(caseID, controlPoint))
  {
    mitkThrowException(SemanticRelationException) << "The control point " << controlPoint.UID << " to add does not exist for the given case.";
  }

  if (!SemanticRelationsInference::InstanceExists(caseID, examinationPeriod))
  {
    mitkThrowException(SemanticRelationException) << "The examination period " << examinationPeriod.UID << " does not exist for the given case. \n Use 'AddExaminationPeriod' before.";
  }

  RelationStorage::AddControlPointToExaminationPeriod(caseID, controlPoint, examinationPeriod);
}

void mitk::SemanticRelationsIntegration::SetInformationType(const DataNode* imageNode, const SemanticTypes::InformationType& informationType)
{
  RemoveInformationTypeFromImage(imageNode);
  AddInformationTypeToImage(imageNode, informationType);

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(imageNode);
  NotifyObserver(caseID);
}

void mitk::SemanticRelationsIntegration::AddInformationTypeToImage(const DataNode* imageNode, const SemanticTypes::InformationType& informationType)
{
  if (nullptr == imageNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid image data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(imageNode);
  SemanticTypes::ID imageID = GetIDFromDataNode(imageNode);
  RelationStorage::AddInformationTypeToImage(caseID, imageID, informationType);
}

void mitk::SemanticRelationsIntegration::RemoveInformationTypeFromImage(const DataNode* imageNode)
{
  if (nullptr == imageNode)
  {
    mitkThrowException(SemanticRelationException) << "Not a valid image data node.";
  }

  SemanticTypes::CaseID caseID = GetCaseIDFromDataNode(imageNode);
  SemanticTypes::ID imageID = GetIDFromDataNode(imageNode);
  SemanticTypes::InformationType originalInformationType = RelationStorage::GetInformationTypeOfImage(caseID, imageID);
  RelationStorage::RemoveInformationTypeFromImage(caseID, imageID);

  // check for further references to the removed information type
  std::vector<std::string> allImageIDsVectorValue = RelationStorage::GetAllImageIDsOfCase(caseID);
  for (const auto& otherImageID : allImageIDsVectorValue)
  {
    SemanticTypes::InformationType otherInformationType = RelationStorage::GetInformationTypeOfImage(caseID, otherImageID);
    if (otherInformationType == originalInformationType)
    {
      // found the information type in another image -> cannot remove the information type from the case
      return;
    }
  }

  // given information type was not referred by any other image of the case -> the information type can be removed from the case
  RelationStorage::RemoveInformationType(caseID, originalInformationType);
}

/************************************************************************/
/* private functions                                                    */
/************************************************************************/
void mitk::SemanticRelationsIntegration::NotifyObserver(const SemanticTypes::CaseID& caseID) const
{
  for (auto& observer : m_ObserverVector)
  {
    observer->Update(caseID);
  }
}

void mitk::SemanticRelationsIntegration::ClearControlPoints(const SemanticTypes::CaseID& caseID)
{
  SemanticTypes::ControlPointVector allControlPointsOfCase = RelationStorage::GetAllControlPointsOfCase(caseID);

  std::vector<std::string> allImageIDsVectorValue = RelationStorage::GetAllImageIDsOfCase(caseID);
  SemanticTypes::ControlPointVector referencedControlPoints;
  for (const auto& imageID : allImageIDsVectorValue)
  {
    auto controlPointOfImage = RelationStorage::GetControlPointOfImage(caseID, imageID);
    referencedControlPoints.push_back(controlPointOfImage);
  }

  std::sort(allControlPointsOfCase.begin(), allControlPointsOfCase.end());
  std::sort(referencedControlPoints.begin(), referencedControlPoints.end());

  SemanticTypes::ControlPointVector nonReferencedControlPoints;
  std::set_difference(allControlPointsOfCase.begin(), allControlPointsOfCase.end(),
                      referencedControlPoints.begin(), referencedControlPoints.end(),
                      std::inserter(nonReferencedControlPoints, nonReferencedControlPoints.begin()));

  auto allExaminationPeriods = RelationStorage::GetAllExaminationPeriodsOfCase(caseID);
  for (const auto& controlPoint : nonReferencedControlPoints)
  {
    const auto& examinationPeriod = FindExaminationPeriod(controlPoint, allExaminationPeriods);
    RelationStorage::RemoveControlPointFromExaminationPeriod(caseID, controlPoint, examinationPeriod);
    RelationStorage::RemoveControlPoint(caseID, controlPoint);
  }
}
