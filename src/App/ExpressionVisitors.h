/***************************************************************************
 *   Copyright (c) Eivind Kvedalen (eivind@kvedalen.name) 2016             *
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

#ifndef RENAMEOBJECTIDENTIFIEREXPRESSIONVISITOR_H
#define RENAMEOBJECTIDENTIFIEREXPRESSIONVISITOR_H

#include <Base/BaseClass.h>
#include "Expression.h"

namespace App {

/**
 * @brief The RenameObjectIdentifierExpressionVisitor class is a functor used to visit each node of an expression, and
 * possibly rename VariableExpression nodes.
 */

template<class P> class RenameObjectIdentifierExpressionVisitor : public ExpressionModifier<P> {
public:
    RenameObjectIdentifierExpressionVisitor(P & _prop,
                                            const std::map<ObjectIdentifier, ObjectIdentifier> &_paths, const ObjectIdentifier & _owner)
        : ExpressionModifier<P>(_prop)
        , paths(_paths)
        , owner(_owner)
    {
    }

    void visit(Expression *node) {
        if(node)
            this->renameObjectIdentifier(*node,paths,owner);
    }


private:
   const std::map<ObjectIdentifier, ObjectIdentifier> &paths; /**< Map with current and new object identifiers */
   const ObjectIdentifier owner;                              /**< Owner of expression */
};

template<class P> class UpdateElementReferenceExpressionVisitor : public ExpressionModifier<P> {
public:

    UpdateElementReferenceExpressionVisitor(P & _prop, App::DocumentObject *feature=0, bool reverse=false)
        : ExpressionModifier<P>(_prop),feature(feature),reverse(reverse)
    {
    }

    void visit(Expression * node) {
        if(node)
            this->updateElementReference(*node,feature,reverse);
    }

private:
    App::DocumentObject *feature;
    bool reverse;
};

template<class P> class RelabelDocumentExpressionVisitor : public ExpressionModifier<P> {
public:

    RelabelDocumentExpressionVisitor(P & prop, const std::string & _oldName, const std::string & _newName)
         : ExpressionModifier<P>(prop)
         , oldName(_oldName)
         , newName(_newName)
    {
    }

    void visit(Expression * node) {
        if(node)
            this->renameDocument(*node,oldName,newName);
    }

private:
    std::string oldName;
    std::string newName;
};

template<class P> class MoveCellsExpressionVisitor : public ExpressionModifier<P> {
public:
    MoveCellsExpressionVisitor(P &prop, const CellAddress &address, int rowCount, int colCount)
        : ExpressionModifier<P>(prop),address(address),rowCount(rowCount),colCount(colCount)
    {}

    void visit(Expression * node) {
        if(node)
            this->moveCells(*node,address,rowCount,colCount);
    }

private:
    CellAddress address;
    int rowCount;
    int colCount;
};

}

#endif // RENAMEOBJECTIDENTIFIEREXPRESSIONVISITOR_H
