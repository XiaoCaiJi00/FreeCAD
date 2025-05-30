/***************************************************************************
 *   Copyright (c) 2013 Jürgen Riegel <FreeCAD@juergen-riegel.net>         *
 *   Copyright (c) 2015 Qingfeng Xia  <FreeCAD@iesensor.com>               *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#include <App/DocumentObjectPy.h>
#include <App/FeaturePythonPyImp.h>

#include "FemSolverObject.h"


using namespace Fem;
using namespace App;

PROPERTY_SOURCE(Fem::FemSolverObject, App::DocumentObject)


FemSolverObject::FemSolverObject()
{
    ADD_PROPERTY_TYPE(Results,
                      (nullptr),
                      "Solver",
                      App::PropertyType(App::Prop_ReadOnly | App::Prop_Output),
                      "Solver results list");
    ADD_PROPERTY_TYPE(WorkingDirectory,
                      (""),
                      "Solver",
                      App::PropertyType(App::Prop_Transient | App::Prop_Hidden | App::Prop_Output),
                      "Solver working directory");
}

FemSolverObject::~FemSolverObject() = default;

short FemSolverObject::mustExecute() const
{
    return 0;
}

PyObject* FemSolverObject::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new DocumentObjectPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}

// Python feature ---------------------------------------------------------

namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(Fem::FemSolverObjectPython, Fem::FemSolverObject)
template<>
const char* Fem::FemSolverObjectPython::getViewProviderName() const
{
    return "FemGui::ViewProviderSolverPython";
}

template<>
PyObject* Fem::FemSolverObjectPython::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new App::FeaturePythonPyT<App::DocumentObjectPy>(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
/// @endcond
// explicit template instantiation
template class FemExport FeaturePythonT<Fem::FemSolverObject>;

}  // namespace App
