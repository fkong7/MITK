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

#include "berryGuiTkISelectionListener.h"

namespace berry {

namespace GuiTk {

void
ISelectionListener::Events
::AddListener(ISelectionListener::Pointer l)
{
  if (l.IsNull()) return;

  selected += Delegate(l.GetPointer(), &ISelectionListener::WidgetSelected);
  defaultSelected += Delegate(l.GetPointer(), &ISelectionListener::WidgetDefaultSelected);
}

void
ISelectionListener::Events
::RemoveListener(ISelectionListener::Pointer l)
{
  if (l.IsNull()) return;

  selected -= Delegate(l.GetPointer(), &ISelectionListener::WidgetSelected);
  defaultSelected -= Delegate(l.GetPointer(), &ISelectionListener::WidgetDefaultSelected);
}

}

}
