/***************************************************************************
 *   Copyright (c) Jürgen Riegel          (juergen.riegel@web.de) 2007     *
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

#include <Base/MatrixPy.h>
#include "DocumentObject.h"
#include "Document.h"
#include "Expression.h"
#include "GroupExtension.h"
#include "GeoFeatureGroupExtension.h"

// inclusion of the generated files (generated out of DocumentObjectPy.xml)
#include <App/DocumentObjectPy.h>
#include <App/DocumentObjectPy.cpp>

using namespace App;

// returns a string which represent the object e.g. when printed in python
std::string DocumentObjectPy::representation(void) const
{
    return std::string("<Document object>");
}

Py::String DocumentObjectPy::getName(void) const
{
    DocumentObject* object = this->getDocumentObjectPtr();
    const char* internal = object->getNameInDocument();
    if (!internal) {
        throw Py::RuntimeError(std::string("This object is currently not part of a document"));
    }
    return Py::String(std::string(internal));
}

Py::Object DocumentObjectPy::getDocument(void) const
{
    DocumentObject* object = this->getDocumentObjectPtr();
    Document* doc = object->getDocument();
    if (!doc) {
        return Py::None();
    }
    else {
        return Py::Object(doc->getPyObject(), true);
    }
}

PyObject*  DocumentObjectPy::addProperty(PyObject *args)
{
    char *sType,*sName=0,*sGroup=0,*sDoc=0;
    short attr=0;
    std::string sDocStr;
    PyObject *ro = Py_False, *hd = Py_False;
    if (!PyArg_ParseTuple(args, "s|ssethO!O!", &sType,&sName,&sGroup,"utf-8",&sDoc,&attr,
        &PyBool_Type, &ro, &PyBool_Type, &hd))     // convert args: Python->C
        return NULL;                             // NULL triggers exception

    if (sDoc) {
        sDocStr = sDoc;
        PyMem_Free(sDoc);
    }

    App::Property* prop=0;
    try {
        prop = getDocumentObjectPtr()->addDynamicProperty(sType,sName,sGroup,sDocStr.c_str(),attr,
            PyObject_IsTrue(ro) ? true : false, PyObject_IsTrue(hd) ? true : false);
    }
    catch (const Base::Exception& e) {
        throw Py::RuntimeError(e.what());
    }
    if (!prop) {
        std::stringstream str;
        str << "No property found of type '" << sType << "'" << std::ends;
        throw Py::Exception(Base::BaseExceptionFreeCADError,str.str());
    }

    return Py::new_reference_to(this);
}

PyObject*  DocumentObjectPy::removeProperty(PyObject *args)
{
    char *sName;
    if (!PyArg_ParseTuple(args, "s", &sName))
        return NULL;

    try {
        bool ok = getDocumentObjectPtr()->removeDynamicProperty(sName);
        return Py_BuildValue("O", (ok ? Py_True : Py_False));
    }
    catch (const Base::Exception& e) {
        throw Py::RuntimeError(e.what());
    }
}

PyObject*  DocumentObjectPy::supportedProperties(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))     // convert args: Python->C
        return NULL;                    // NULL triggers exception

    std::vector<Base::Type> ary;
    Base::Type::getAllDerivedFrom(App::Property::getClassTypeId(), ary);
    Py::List res;
    for (std::vector<Base::Type>::iterator it = ary.begin(); it != ary.end(); ++it) {
        Base::BaseClass *data = static_cast<Base::BaseClass*>(it->createInstance());
        if (data) {
            delete data;
            res.append(Py::String(it->getName()));
        }
    }
    return Py::new_reference_to(res);
}

PyObject*  DocumentObjectPy::touch(PyObject * args)
{
    if (!PyArg_ParseTuple(args, ""))     // convert args: Python->C 
        return NULL;                    // NULL triggers exception 
    getDocumentObjectPtr()->touch();
    Py_Return;
}

PyObject*  DocumentObjectPy::purgeTouched(PyObject * args)
{
    if (!PyArg_ParseTuple(args, ""))     // convert args: Python->C 
        return NULL;                    // NULL triggers exception 
    getDocumentObjectPtr()->purgeTouched();
    Py_Return;
}

Py::List DocumentObjectPy::getState(void) const
{
    DocumentObject* object = this->getDocumentObjectPtr();
    Py::List list;
    bool uptodate = true;
    if (object->isTouched()) {
        uptodate = false;
        list.append(Py::String("Touched"));
    }
    if (object->isError()) {
        uptodate = false;
        list.append(Py::String("Invalid"));
    }
    if (object->isRecomputing()) {
        uptodate = false;
        list.append(Py::String("Recompute"));
    }
    if (object->isRestoring()) {
        uptodate = false;
        list.append(Py::String("Restore"));
    }
    if (object->testStatus(App::Expand)){
        list.append(Py::String("Expanded"));
    }
    if (uptodate) {
        list.append(Py::String("Up-to-date"));
    }
    return list;
}

Py::Object DocumentObjectPy::getViewObject(void) const
{
    try {
        Py::Module module(PyImport_ImportModule("FreeCADGui"),true);
        if (!module.hasAttr("getDocument")) {
            // in v0.14+, the GUI module can be loaded in console mode (but doesn't have all its document methods)
            return Py::None();
        }
        Py::Callable method(module.getAttr("getDocument"));
        Py::Tuple arg(1);
        arg.setItem(0, Py::String(getDocumentObjectPtr()->getDocument()->getName()));
        Py::Object doc = method.apply(arg);
        method = doc.getAttr("getObject");
        arg.setItem(0, Py::String(getDocumentObjectPtr()->getNameInDocument()));
        Py::Object obj = method.apply(arg);
        return obj;
    }
    catch (Py::Exception& e) {
        if (PyErr_ExceptionMatches(PyExc_ImportError)) {
            // the GUI is not up, hence None is returned
            e.clear();
            return Py::None();
        }
        // FreeCADGui is loaded, so there must be wrong something else
        throw; // re-throw
    }
}

Py::List DocumentObjectPy::getInList(void) const
{
    Py::List ret;
    std::vector<DocumentObject*> list = getDocumentObjectPtr()->getInList();

    for (std::vector<DocumentObject*>::iterator It=list.begin();It!=list.end();++It)
        ret.append(Py::Object((*It)->getPyObject(), true));

    return ret;
}

Py::List DocumentObjectPy::getInListRecursive(void) const
{
    Py::List ret;
    try {
        std::vector<DocumentObject*> list = getDocumentObjectPtr()->getInListRecursive();

        for (std::vector<DocumentObject*>::iterator It = list.begin(); It != list.end(); ++It)
            ret.append(Py::Object((*It)->getPyObject(), true));
 
    }
    catch (const Base::Exception& e) {
        throw Py::IndexError(e.what());
    }
    return ret;    
}

Py::List DocumentObjectPy::getOutList(void) const
{
    Py::List ret;
    std::vector<DocumentObject*> list = getDocumentObjectPtr()->getOutList();

    for (std::vector<DocumentObject*>::iterator It=list.begin();It!=list.end();++It)
        ret.append(Py::Object((*It)->getPyObject(), true));

    return ret;
}

Py::List DocumentObjectPy::getOutListRecursive(void) const
{
    Py::List ret;
    try {
        std::vector<DocumentObject*> list = getDocumentObjectPtr()->getOutListRecursive();

        // creat the python list for the output
        for (std::vector<DocumentObject*>::iterator It = list.begin(); It != list.end(); ++It)
            ret.append(Py::Object((*It)->getPyObject(), true));
    }
    catch (const Base::Exception& e) {
        throw Py::IndexError(e.what());
    }

    return ret;
}

PyObject*  DocumentObjectPy::setExpression(PyObject * args)
{
    char * path = NULL;
    PyObject * expr;
    char * comment = 0;

    if (!PyArg_ParseTuple(args, "sO|s", &path, &expr, &comment))     // convert args: Python->C
        return NULL;                    // NULL triggers exception

    App::ObjectIdentifier p(ObjectIdentifier::parse(getDocumentObjectPtr(), path));

    if (Py::Object(expr).isNone())
        getDocumentObjectPtr()->setExpression(p, boost::shared_ptr<Expression>());
#if PY_MAJOR_VERSION >= 3
    else if (PyUnicode_Check(expr)) {
        const char * exprStr = PyUnicode_AsUTF8(expr);
#else
    else if (PyString_Check(expr)) {
        const char * exprStr = PyString_AsString(expr);
#endif
        boost::shared_ptr<Expression> shared_expr(ExpressionParser::parse(getDocumentObjectPtr(), exprStr));

        getDocumentObjectPtr()->setExpression(p, shared_expr, comment);
    }
    else if (PyUnicode_Check(expr)) {
#if PY_MAJOR_VERSION >= 3
        std::string exprStr = PyUnicode_AsUTF8(expr);
#else
        PyObject* unicode = PyUnicode_AsEncodedString(expr, "utf-8", 0);
        if (unicode) {
            std::string exprStr = PyString_AsString(unicode);
            Py_DECREF(unicode);
            boost::shared_ptr<Expression> shared_expr(ExpressionParser::parse(getDocumentObjectPtr(), exprStr.c_str()));

            getDocumentObjectPtr()->setExpression(p, shared_expr, comment);
        }
        else {
            // utf-8 encoding failed
            return 0;
        }
#endif
    }
    else
        throw Py::TypeError("String or None expected.");
    Py_Return;
}

PyObject*  DocumentObjectPy::recompute(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    try {
        bool ok = getDocumentObjectPtr()->recomputeFeature();
        return Py_BuildValue("O", (ok ? Py_True : Py_False));
    }
    catch (const Base::Exception& e) {
        throw Py::RuntimeError(e.what());
    }
}

PyObject*  DocumentObjectPy::getSubObject(PyObject *args, PyObject *keywds)
{
    PyObject *obj;
    short retType = 0;
    PyObject *pyMat = Py_None;
    PyObject *doTransform = Py_True;
    short depth = 0;
    static char *kwlist[] = {"subname","retType","matrix","transform","depth", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, keywds, "O|hOOh", kwlist,
                &obj,&retType,&pyMat,&doTransform,&depth))
        return 0;

    if(retType<0 || retType>2) {
        PyErr_SetString(PyExc_TypeError, "invalid retType, can only be integer 0, 1 or 2");
        return 0;
    }

    std::vector<std::string> subs;
    bool single=true;
    if (PyUnicode_Check(obj)) {
#if PY_MAJOR_VERSION >= 3
        string = PyUnicode_AsUTF8(obj);
#else
        PyObject* unicode = PyUnicode_AsUTF8String(obj);
        subs.push_back(PyString_AsString(unicode));
        Py_DECREF(unicode);
    }
    else if (PyString_Check(obj)) {
        subs.push_back(PyString_AsString(obj));
#endif
    } else if (PySequence_Check(obj)) {
        single=false;
        Py::Sequence shapeSeq(obj);
        for (Py::Sequence::iterator it = shapeSeq.begin(); it != shapeSeq.end(); ++it) {
            PyObject* item = (*it).ptr();
            if (PyUnicode_Check(item)) {
#if PY_MAJOR_VERSION >= 3
                string = PyUnicode_AsUTF8(item);
#else
                PyObject* unicode = PyUnicode_AsUTF8String(item);
                subs.push_back(PyString_AsString(unicode));
                Py_DECREF(unicode);
            }
            else if (PyString_Check(item)) {
                subs.push_back(PyString_AsString(item));
#endif
            }else{
                PyErr_SetString(PyExc_TypeError, "non-string object in sequence");
                return 0;
            }
        }
    }else{
        PyErr_SetString(PyExc_TypeError, "subname must be either a string or sequence of string");
        return 0;
    }

    bool transform = PyObject_IsTrue(doTransform);

    struct SubInfo {
        PyObject *obj;
        PyObject *pyObj;
        std::string subname;
        Base::Matrix4D mat;
        SubInfo(const Base::Matrix4D &mat):obj(Py_None),pyObj(Py_None),mat(mat)
        {}
    };

    Base::Matrix4D mat;
    if(pyMat!=Py_None) {
        if(!PyObject_TypeCheck(pyMat,&Base::MatrixPy::Type)) {
            PyErr_SetString(PyExc_TypeError, "expect argument 'matrix' to be of type Base.Matrix");
            return 0;
        }
        mat = *static_cast<Base::MatrixPy*>(pyMat)->getMatrixPtr();
    }

    std::vector<SubInfo> ret;
    for(const auto &sub : subs) {
        const char *subname = 0;
        SubInfo info(mat);
        auto obj = getDocumentObjectPtr()->getSubObject(
                sub.c_str(),&subname,retType==1?0:&info.pyObj,&info.mat,transform,depth);
        if(obj) {
            if(subname) info.subname = subname;
            info.obj = obj->getPyObject();
        }
        ret.push_back(info);
    }
    if(ret.empty())
        Py_Return;

    if(single) {
        if(retType==0) {
            if(ret[0].pyObj == Py_None)
                Py_Return;
            return ret[0].pyObj;
        }
        Py::Tuple rret(retType==1?3:4);
        rret.setItem(0,Py::Object(ret[0].obj,ret[0].obj!=Py_None));
        rret.setItem(1,Py::String(ret[0].subname));
        rret.setItem(2,Py::Object(new Base::MatrixPy(ret[0].mat)));
        if(retType!=1)
            rret.setItem(3,Py::Object(ret[0].pyObj,ret[0].pyObj!=Py_None));
        return Py::new_reference_to(rret);
    }
    Py::Tuple tuple(ret.size());
    for(size_t i=0;i<ret.size();++i) {
        if(retType==0)
            tuple.setItem(i,Py::Object(ret[i].pyObj,ret[i].pyObj!=Py_None));
        else {
            Py::Tuple rret(retType==1?3:4);
            rret.setItem(0,Py::Object(ret[i].obj,ret[i].obj!=Py_None));
            rret.setItem(1,Py::String(ret[i].subname));
            rret.setItem(2,Py::Object(new Base::MatrixPy(ret[i].mat)));
            if(retType!=1)
                rret.setItem(3,Py::Object(ret[i].pyObj,ret[i].pyObj!=Py_None));
            tuple.setItem(i,rret);
        }
    }
    return Py::new_reference_to(tuple);
}

PyObject*  DocumentObjectPy::getLinkedObject(PyObject *args, PyObject *keywds)
{
    PyObject *recursive = Py_True;
    PyObject *pyMat = Py_None;
    PyObject *transform = Py_True;
    short depth = 0;
    static char *kwlist[] = {"recursive","matrix","transform","depth", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|OOOh", kwlist,
                &recursive,&pyMat,&transform,&depth))
        return NULL;

    Base::Matrix4D _mat;
    Base::Matrix4D *mat = 0;
    if(pyMat!=Py_None) {
        if(!PyObject_TypeCheck(pyMat,&Base::MatrixPy::Type)) {
            PyErr_SetString(PyExc_TypeError, "expect argument 'matrix' to be of type Base.Matrix");
            return 0;
        }
        _mat = *static_cast<Base::MatrixPy*>(pyMat)->getMatrixPtr();
        mat = &_mat;
    }
    auto linked = getDocumentObjectPtr()->getLinkedObject(
            PyObject_IsTrue(recursive), mat, PyObject_IsTrue(transform),depth);
    if(!linked)
        linked = getDocumentObjectPtr();
    auto pyObj = Py::Object(linked->getPyObject(),true);
    if(mat) {
        Py::Tuple ret(2);
        ret.setItem(0,pyObj);
        ret.setItem(1,Py::Object(new Base::MatrixPy(*mat)));
        return Py::new_reference_to(ret);
    }
    return Py::new_reference_to(pyObj);
}

PyObject*  DocumentObjectPy::getParentGroup(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    try {
        auto grp = GroupExtension::getGroupOfObject(getDocumentObjectPtr());
        if(!grp) {
            Py_INCREF(Py_None);
            return Py_None;
        }
        return grp->getPyObject();
    }
    catch (const Base::Exception& e) {
        throw Py::RuntimeError(e.what());
    }
}

PyObject*  DocumentObjectPy::getParentGeoFeatureGroup(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    try {
        auto grp = GeoFeatureGroupExtension::getGroupOfObject(getDocumentObjectPtr());
        if(!grp) {
            Py_INCREF(Py_None);
            return Py_None;
        }
        return grp->getPyObject();
    }
    catch (const Base::Exception& e) {
        throw Py::RuntimeError(e.what());
    }
}

PyObject *DocumentObjectPy::getCustomAttributes(const char* attr) const
{
    // search for dynamic property
    Property* prop = getDocumentObjectPtr()->getDynamicPropertyByName(attr);
    if (prop)
        return prop->getPyObject();
    else
        return 0;
}

int DocumentObjectPy::setCustomAttributes(const char* attr, PyObject *obj)
{
    // explicitly search for dynamic property
    try {
        Property* prop = getDocumentObjectPtr()->getDynamicPropertyByName(attr);
        if (prop) {
            prop->setPyObject(obj);
            return 1;
        }
    }
    catch (Base::ValueError &exc) {
        std::stringstream s;
        s << "Property '" << attr << "': " << exc.what();
        throw Py::ValueError(s.str());
    }
    catch (Base::Exception &exc) {
        std::stringstream s;
        s << "Attribute (Name: " << attr << ") error: '" << exc.what() << "' ";
        throw Py::AttributeError(s.str());
    }
    catch (...) {
        std::stringstream s;
        s << "Unknown error in attribute " << attr;
        throw Py::AttributeError(s.str());
    }

    // search in PropertyList
    Property *prop = getDocumentObjectPtr()->getPropertyByName(attr);
    if (prop) {
        // Read-only attributes must not be set over its Python interface
        short Type =  getDocumentObjectPtr()->getPropertyType(prop);
        if (Type & Prop_ReadOnly) {
            std::stringstream s;
            s << "'DocumentObject' attribute '" << attr << "' is read-only"; 
            throw Py::AttributeError(s.str());
        }

        try {
            prop->setPyObject(obj);
        }
        catch (const Base::TypeError& e) {
            std::stringstream s;
            s << "Property '" << prop->getName() << "': " << e.what();
            throw Py::TypeError(s.str());
        }
        return 1;
    } 

    return 0;
}
