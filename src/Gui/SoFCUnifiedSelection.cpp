/***************************************************************************
 *   Copyright (c) 2005 Jürgen Riegel <juergen.riegel@web.de>              *
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

#ifndef _PreComp_
# include <qstatusbar.h>
# include <qstring.h>
# include <Inventor/details/SoFaceDetail.h>
# include <Inventor/details/SoLineDetail.h>
#endif

#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoWindowElement.h>

#include <Inventor/SoFullPath.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoHandleEventAction.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/elements/SoComplexityElement.h>
#include <Inventor/elements/SoComplexityTypeElement.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/elements/SoElements.h>
#include <Inventor/elements/SoFontNameElement.h>
#include <Inventor/elements/SoFontSizeElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoProfileCoordinateElement.h>
#include <Inventor/elements/SoProfileElement.h>
#include <Inventor/elements/SoSwitchElement.h>
#include <Inventor/elements/SoUnitsElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/misc/SoState.h>
#include <Inventor/misc/SoChildList.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/threads/SbStorage.h>

#ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
#else
# ifdef FC_OS_WIN32
#  include <windows.h>
# endif
# include <GL/gl.h>
#endif

#include <Inventor/SbDPLine.h>

#include <QtOpenGL.h>

#include <Base/Console.h>
#include <Base/Tools.h>
#include <App/Application.h>
#include <App/Document.h>
#include <Gui/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObserver.h>
#include <App/ComplexGeoData.h>

#include "SoFCUnifiedSelection.h"
#include "Application.h"
#include "MainWindow.h"
#include "Selection.h"
#include "ViewProvider.h"
#include "SoFCInteractiveElement.h"
#include "SoFCSelectionAction.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderGeometryObject.h"
#include "ViewParams.h"

FC_LOG_LEVEL_INIT("SoFCUnifiedSelection",false,true)

using namespace Gui;

SoFullPath * Gui::SoFCUnifiedSelection::currenthighlight = NULL;

// *************************************************************************

SO_NODE_SOURCE(SoFCUnifiedSelection)

/*!
  Constructor.
*/
SoFCUnifiedSelection::SoFCUnifiedSelection() : pcDocument(0), pcViewer(0), pcRayPick(0)
{
    SO_NODE_CONSTRUCTOR(SoFCUnifiedSelection);

    SO_NODE_ADD_FIELD(colorHighlight, (SbColor(1.0f, 0.6f, 0.0f)));
    SO_NODE_ADD_FIELD(colorSelection, (SbColor(0.1f, 0.8f, 0.1f)));
    SO_NODE_ADD_FIELD(highlightMode,  (AUTO));
    SO_NODE_ADD_FIELD(selectionMode,  (ON));
    SO_NODE_ADD_FIELD(selectionRole,  (true));
    SO_NODE_ADD_FIELD(useNewSelection, (true));

    SO_NODE_DEFINE_ENUM_VALUE(HighlightModes, AUTO);
    SO_NODE_DEFINE_ENUM_VALUE(HighlightModes, ON);
    SO_NODE_DEFINE_ENUM_VALUE(HighlightModes, OFF);
    SO_NODE_SET_SF_ENUM_TYPE (highlightMode, HighlightModes);

    detailPath = static_cast<SoFullPath*>(new SoPath(20));
    detailPath->ref();

    setPreSelection = false;
    preSelection = -1;
    useNewSelection = ViewParams::instance()->getUseNewSelection();

    preselTime = SbTime::getTimeOfDay();
    preselTimer.setData(this);
    preselTimer.setFunction([](void *data, SoSensor*){
        reinterpret_cast<SoFCUnifiedSelection*>(data)->onPreselectTimer();
    });

    pcRayPick = new SoFCRayPickAction;
}

/*!
  Destructor.
*/
SoFCUnifiedSelection::~SoFCUnifiedSelection()
{
    // If we're being deleted and we're the current highlight,
    // NULL out that variable
    if (currenthighlight != NULL) {
        currenthighlight->unref();
        currenthighlight = NULL;
    }
    if (detailPath) {
        detailPath->unref();
        detailPath = NULL;
    }

    delete pcRayPick;
}

// doc from parent
void
SoFCUnifiedSelection::initClass(void)
{
    SO_NODE_INIT_CLASS(SoFCUnifiedSelection,SoSeparator,"Separator");
}

void SoFCUnifiedSelection::finish()
{
    atexit_cleanup();
}

bool SoFCUnifiedSelection::hasHighlight() {
    return currenthighlight != NULL;
}

void SoFCUnifiedSelection::applySettings()
{
    this->highlightMode = ViewParams::instance()->getEnablePreselection()?ON:OFF;
    this->selectionMode = ViewParams::instance()->getEnableSelection()?ON:OFF;

    float trans;
    SbColor color;
    color.setPackedValue(ViewParams::instance()->getHighlightColor(),trans);
    this->colorHighlight = color;

    color.setPackedValue(ViewParams::instance()->getSelectionColor(),trans);
    this->colorSelection = color;
}

const char* SoFCUnifiedSelection::getFileFormatName(void) const
{
    return "Separator";
}

void SoFCUnifiedSelection::write(SoWriteAction * action)
{
    SoOutput * out = action->getOutput();
    if (out->getStage() == SoOutput::WRITE) {
        // Do not write out the fields of this class
        if (this->writeHeader(out, true, false)) return;
        SoGroup::doAction((SoAction *)action);
        this->writeFooter(out);
    }
    else {
        inherited::write(action);
    }
}

int SoFCUnifiedSelection::getPriority(const SoPickedPoint* p)
{
    const SoDetail* detail = p->getDetail();
    if (!detail)                                           return 0;
    if (detail->isOfType(SoFaceDetail::getClassTypeId()))  return 1;
    if (detail->isOfType(SoLineDetail::getClassTypeId()))  return 2;
    if (detail->isOfType(SoPointDetail::getClassTypeId())) return 3;
    return 0;
}

struct SoFCUnifiedSelection::PickedInfo {
    std::unique_ptr<SoPickedPoint> ppCopy;
    const SoPickedPoint *pp;
    ViewProviderDocumentObject *vpd;
    std::string element;

    PickedInfo()
        :pp(0),vpd(0)
    {}

    PickedInfo(PickedInfo &&other)
        :ppCopy(std::move(other.ppCopy))
        ,pp(other.pp)
        ,vpd(other.vpd)
        ,element(std::move(other.element))
    {}

    PickedInfo &operator=(PickedInfo &&other) {
        ppCopy = std::move(other.ppCopy);
        pp = other.pp;
        vpd = other.vpd;
        element = std::move(other.element);
        return *this;
    }

    void copy() {
        ppCopy.reset(pp->copy());
        pp = ppCopy.get();
    }

};

void SoFCUnifiedSelection::getPickedInfo(std::vector<PickedInfo> &ret,
        const SoPickedPointList &points, bool singlePick, bool copy,
        std::set<std::pair<ViewProvider*,std::string> > &filter) const
{
    ViewProvider *last_vp = 0;
    for(int i=0,count=points.getLength();i<count;++i) {
        PickedInfo info;
        info.pp = points[i];
        info.vpd = 0;
        ViewProvider *vp = 0;
        SoFullPath *path = static_cast<SoFullPath *>(info.pp->getPath());
        if (this->pcDocument && path) {
            vp = this->pcDocument->getViewProviderByPathFromHead(path);
            if(singlePick && last_vp && last_vp!=vp)
                return;
        }
        if(!vp || !vp->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
            if(!singlePick) continue;
            if(ret.empty()) {
                if(copy) info.copy();
                ret.push_back(std::move(info));
            }
            break;
        }
        info.vpd = static_cast<ViewProviderDocumentObject*>(vp);
        if(!(useNewSelection.getValue()||info.vpd->useNewSelectionModel()) || !info.vpd->isSelectable()) {
            if(!singlePick) continue;
            if(ret.empty()) {
                info.vpd = 0;
                if(copy) info.copy();
                ret.push_back(std::move(info));
            }
            break;
        }
        if(!info.vpd->getElementPicked(info.pp,info.element))
            continue;

        if(singlePick) 
            last_vp = vp;
        else if(!filter.emplace(info.vpd,info.element).second)
            continue;
        
        if(copy) info.copy();
        ret.push_back(std::move(info));
    }
}

void SoFCUnifiedSelection::getPickedInfoOnTop(std::vector<PickedInfo> &ret,
        bool singlePick, std::set<std::pair<ViewProvider*,std::string> > &filter) const
{
    if(ViewParams::instance()->getShowSelectionBoundingBox())
        return;

    SoPath *path = pcViewer->getGroupOnTopPath();
    int pathLength = path->getLength();
    if(!pathLength || !path->getNodeFromTail(0)->isOfType(SoGroup::getClassTypeId()))
        return;

    SoGroup *group = static_cast<SoGroup*>(path->getNodeFromTail(0));

    for(int i=0,count=group->getNumChildren();i<count;++i) {
        auto child = group->getChild(i);
        if(!child->isOfType(SoFCPathAnnotation::getClassTypeId()))
            continue;
        static_cast<SoFCPathAnnotation*>(child)->doPick(path,pcRayPick);
        getPickedInfo(ret,pcRayPick->getPrioPickedPointList(),singlePick,true,filter);
        pcRayPick->cleanup();
        if(singlePick && ret.size()>=1)
            break;
    }
    path->truncate(pathLength);
}

std::vector<SoFCUnifiedSelection::PickedInfo> 
SoFCUnifiedSelection::getPickedList(SoHandleEventAction* action, bool singlePick) const {
    return getPickedList(action->getEvent()->getPosition(),action->getViewportRegion(), singlePick);
}

std::vector<SoFCUnifiedSelection::PickedInfo> 
SoFCUnifiedSelection::getPickedList(const SbVec2s &pos, const SbViewportRegion &viewport, bool singlePick) const
{
    std::vector<PickedInfo> ret;
    std::set<std::pair<ViewProvider*,std::string> > filter;

    FC_TIME_INIT(t);

    float radius = ViewParams::instance()->getPickRadius();
    pcRayPick->setRadius(radius);
    pcRayPick->setViewportRegion(viewport);
    pcRayPick->setPoint(pos);
    pcRayPick->setPickAll(!singlePick || !ViewParams::instance()->getUseNewRayPick());

    SoPickStyleElement::set(pcRayPick->getState(),
            singlePick ? SoPickStyleElement::SHAPE_FRONTFACES : SoPickStyleElement::SHAPE);
    SoOverrideElement::setPickStyleOverride(pcRayPick->getState(),0,true);

    getPickedInfoOnTop(ret, singlePick, filter);

    if(ret.empty() || !singlePick) {
        SoOverrideElement::setPickStyleOverride(pcRayPick->getState(),0,false);
        pcRayPick->apply(pcViewer->getSoRenderManager()->getSceneGraph());

        getPickedInfo(ret,pcRayPick->getPrioPickedPointList(),singlePick,false,filter);
    }

    FC_TIME_TRACE(t,"pick radius " << radius << ", count " << ret.size() << ',');

    // postProcessPickedList() is used to resolve overlapping primitives
    // (point, line and face). We will pick point over line over face if
    // the picked points overalps.
    //
    // The disadvantage of doing post process here is that we must obtain all
    // picked points of all objects. When the user zooms the camera far out,
    // the pick radius (default to 5 pixel) may cause large amount (100k+ for
    // bigger assembly) of primitves (mostly points and lines) in the picked
    // point list. The fact that each SoPickedPoint contains a full path of the
    // node hierarchy may cause system slow down, with most of the time
    // spending on cleaning up the path.
    //
    // This problem can be solved by replacing SoRayPickAction with a derived
    // SoFCRayPickAction. After picking each shape node, it will consider the
    // primitive priority before choosing which picked point to retain (see
    // SoFCRayPickAction::afterPick()). With this action, all picked points are
    // processed on the fly, and only the primitive with the closest picked
    // point and the highest priority will be returned.
    //
    if(singlePick && pcRayPick->isPickAll())
        postProcessPickedList(ret, singlePick);
    return ret;
}

void SoFCUnifiedSelection::postProcessPickedList(std::vector<PickedInfo> &ret, bool singlePick) {
    if(ret.size()<=1)
        return;

    // To identify the picking of lines in a concave area we have to 
    // get all intersection points. If we have two or more intersection
    // points where the first is of a face and the second of a line with
    // almost similar coordinates we use the second point, instead.

    int picked_prio = getPriority(ret[0].pp);
    auto last_vpd = ret[0].vpd;
    const SbVec3f& picked_pt = ret.front().pp->getPoint();
    auto itPicked = ret.begin();
    for(auto it=ret.begin()+1;it!=ret.end();++it) {
        auto &info = *it;
        if(last_vpd != info.vpd) 
            break;

        int cur_prio = getPriority(info.pp);
        const SbVec3f& cur_pt = info.pp->getPoint();

        if ((cur_prio > picked_prio) && picked_pt.equals(cur_pt, 0.01f)) {
            itPicked = it;
            picked_prio = cur_prio;
        }
    }

    if(singlePick) {
        std::vector<PickedInfo> sret(1);
        sret[0] = std::move(*itPicked);
        ret = std::move(sret);
        return;
    }
    if(itPicked != ret.begin()) {
        PickedInfo tmp(std::move(*itPicked));
        *itPicked = std::move(*ret.begin());
        *ret.begin() = std::move(tmp);
    }
}

std::vector<App::SubObjectT>
SoFCUnifiedSelection::getPickedSelections(const SbVec2s &pos,
        const SbViewportRegion &viewport, bool singlePick) const
{
    std::vector<App::SubObjectT> sels;
    auto infos = getPickedList(pos,viewport,singlePick);
    sels.reserve(infos.size());
    for(auto &info : infos) {
        if(info.vpd)
            sels.emplace_back(info.vpd->getObject(),info.element.c_str());
    }
    return sels;
}

SoPickedPoint *SoFCUnifiedSelection::getPickedPoint(SoHandleEventAction *action) const {
    auto res = getPickedList(action,true);
    if(res.size() && res[0].pp)
        return res[0].pp->copy();
    return 0;
}

void SoFCUnifiedSelection::doAction(SoAction *action)
{
    if (action->getTypeId() == SoFCEnableHighlightAction::getClassTypeId()) {
        SoFCEnableHighlightAction *preaction = (SoFCEnableHighlightAction*)action;
        if (preaction->highlight) {
            this->highlightMode = SoFCUnifiedSelection::AUTO;
        }
        else {
            this->highlightMode = SoFCUnifiedSelection::OFF;
        }
    }

    if (action->getTypeId() == SoFCEnableSelectionAction::getClassTypeId()) {
        SoFCEnableSelectionAction *selaction = (SoFCEnableSelectionAction*)action;
        if (selaction->selection) {
            this->selectionMode = SoFCUnifiedSelection::ON;
        }
        else {
            this->selectionMode = SoFCUnifiedSelection::OFF;
        }
    }

    if (action->getTypeId() == SoFCSelectionColorAction::getClassTypeId()) {
        SoFCSelectionColorAction *colaction = (SoFCSelectionColorAction*)action;
        this->colorSelection = colaction->selectionColor;
    }

    if (action->getTypeId() == SoFCHighlightColorAction::getClassTypeId()) {
        SoFCHighlightColorAction *colaction = (SoFCHighlightColorAction*)action;
        this->colorHighlight = colaction->highlightColor;
    }

    if (action->getTypeId() == SoFCHighlightAction::getClassTypeId()) {
        SoFCHighlightAction *hilaction = static_cast<SoFCHighlightAction*>(action);
        // Do not clear currently highlighted object when setting new pre-selection
        if (!setPreSelection && hilaction->SelChange.Type == SelectionChanges::RmvPreselect) {
            if (currenthighlight) {
                SoHighlightElementAction action;
                action.apply(currenthighlight);
                currenthighlight->unref();
                currenthighlight = 0;
            }
        } else if (highlightMode.getValue() != OFF 
                    && hilaction->SelChange.Type == SelectionChanges::SetPreselect) {
            if (currenthighlight) {
                SoHighlightElementAction action;
                action.apply(currenthighlight);
                currenthighlight->unref();
                currenthighlight = 0;
            }
            App::Document* doc = App::GetApplication().getDocument(hilaction->SelChange.pDocName);
            App::DocumentObject* obj = doc->getObject(hilaction->SelChange.pObjectName);
            ViewProvider*vp = Application::Instance->getViewProvider(obj);
            SoDetail* detail = vp->getDetail(hilaction->SelChange.pSubName);
            SoHighlightElementAction action;
            action.setHighlighted(true);
            action.setColor(this->colorHighlight.getValue());
            action.setElement(detail);
            action.apply(vp->getRoot());
            delete detail;
            SoSearchAction sa;
            sa.setNode(vp->getRoot());
            sa.apply(vp->getRoot());
            currenthighlight = static_cast<SoFullPath*>(sa.getPath()->copy());
            currenthighlight->ref();
        }
        if(useNewSelection.getValue())
            return;
    }

    if (action->getTypeId() == SoFCSelectionAction::getClassTypeId()) {
        SoFCSelectionAction *selaction = static_cast<SoFCSelectionAction*>(action);
        if(selectionMode.getValue() == ON 
            && (selaction->SelChange.Type == SelectionChanges::AddSelection 
                || selaction->SelChange.Type == SelectionChanges::RmvSelection))
        {
            // selection changes inside the 3d view are handled in handleEvent()
            App::Document* doc = App::GetApplication().getDocument(selaction->SelChange.pDocName);
            App::DocumentObject* obj = doc->getObject(selaction->SelChange.pObjectName);
            ViewProvider*vp = Application::Instance->getViewProvider(obj);
            if (vp && (useNewSelection.getValue()||vp->useNewSelectionModel()) && vp->isSelectable()) {
                SoDetail *detail = nullptr;
                detailPath->truncate(0);
                if(!selaction->SelChange.pSubName || !selaction->SelChange.pSubName[0] ||
                    vp->getDetailPath(selaction->SelChange.pSubName,detailPath,true,detail))
                {
                    SoSelectionElementAction::Type type = SoSelectionElementAction::None;
                    if (selaction->SelChange.Type == SelectionChanges::AddSelection) {
                        if (detail)
                            type = SoSelectionElementAction::Append;
                        else
                            type = SoSelectionElementAction::All;
                    }
                    else {
                        if (detail)
                            type = SoSelectionElementAction::Remove;
                        else
                            type = SoSelectionElementAction::None;
                    }

                    SoSelectionElementAction action(type);
                    action.setColor(this->colorSelection.getValue());
                    action.setElement(detail);
                    if(detailPath->getLength())
                        action.apply(detailPath);
                    else
                        action.apply(vp->getRoot());
                }
                detailPath->truncate(0);
                delete detail;
            }
        }else if (selaction->SelChange.Type == SelectionChanges::ClrSelection) {
            SoSelectionElementAction action(SoSelectionElementAction::None);
            for(int i=0;i<this->getNumChildren();++i)
                action.apply(this->getChild(i));
        }else if(selectionMode.getValue() == ON 
                    && selaction->SelChange.Type == SelectionChanges::SetSelection) {
            std::vector<ViewProvider*> vps;
            if (this->pcDocument)
                vps = this->pcDocument->getViewProvidersOfType(ViewProviderDocumentObject::getClassTypeId());
            for (std::vector<ViewProvider*>::iterator it = vps.begin(); it != vps.end(); ++it) {
                ViewProviderDocumentObject* vpd = static_cast<ViewProviderDocumentObject*>(*it);
                if (useNewSelection.getValue() || vpd->useNewSelectionModel()) {
                    SoSelectionElementAction::Type type;
                    if(Selection().isSelected(vpd->getObject()) && vpd->isSelectable())
                        type = SoSelectionElementAction::All;
                    else
                        type = SoSelectionElementAction::None;
                    SoSelectionElementAction action(type);
                    action.setColor(this->colorSelection.getValue());
                    action.apply(vpd->getRoot());
                }
            }
        } else if (selaction->SelChange.Type == SelectionChanges::SetPreselectSignal) {
            // selection changes inside the 3d view are handled in handleEvent()
            App::Document* doc = App::GetApplication().getDocument(selaction->SelChange.pDocName);
            App::DocumentObject* obj = doc->getObject(selaction->SelChange.pObjectName);
            ViewProvider*vp = Application::Instance->getViewProvider(obj);
            if (vp && vp->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId()) &&
                (useNewSelection.getValue()||vp->useNewSelectionModel()) && vp->isSelectable()) 
            {
                detailPath->truncate(0);
                SoDetail *det = 0;
                if(vp->getDetailPath(selaction->SelChange.pSubName,detailPath,true,det)) {
                    setHighlight(detailPath,det,static_cast<ViewProviderDocumentObject*>(vp),
                            selaction->SelChange.pSubName, 
                            selaction->SelChange.x,
                            selaction->SelChange.y,
                            selaction->SelChange.z);
                }
                delete det;
            }
        }
        if(useNewSelection.getValue())
            return;
    }

    inherited::doAction( action );
}

void SoFCUnifiedSelection::onPreselectTimer() {
    if(preselTimer.isScheduled())
        preselTimer.unschedule();

    auto infos = getPickedList(preselPos, preselViewport, true);
    if(infos.size())
        setHighlight(infos[0]);
    else
        setHighlight(PickedInfo());

    preselTime = SbTime::getTimeOfDay();
}

void SoFCUnifiedSelection::removeHighlight() {
    if(this->preSelection == 1)
        setHighlight(0,0,0,0,0.0,0.0,0.0);
    pcRayPick->cleanup();
}

bool SoFCUnifiedSelection::setHighlight(const PickedInfo &info) {
    if(!info.pp) {
        return setHighlight(0,0,0,0,0.0,0.0,0.0);
    }
    const auto &pt = info.pp->getPoint();
    return setHighlight(static_cast<SoFullPath*>(info.pp->getPath()), 
            info.pp->getDetail(), info.vpd, info.element.c_str(), pt[0],pt[1],pt[2]);
}

bool SoFCUnifiedSelection::setHighlight(SoFullPath *path, const SoDetail *det, 
        ViewProviderDocumentObject *vpd, const char *element, float x, float y, float z) 
{
    Base::FlagToggler<SbBool> flag(setPreSelection);

    bool highlighted = false;
    if(path && path->getLength() && 
       vpd && vpd->getObject() && vpd->getObject()->getNameInDocument()) 
    {
        const char *docname = vpd->getObject()->getDocument()->getName();
        const char *objname = vpd->getObject()->getNameInDocument();

        this->preSelection = 1;
        static char buf[513];
        snprintf(buf,512,"Preselected: %s.%s.%s (%g, %g, %g)"
                ,docname,objname,element
                ,fabs(x)>1e-7?x:0.0
                ,fabs(y)>1e-7?y:0.0
                ,fabs(z)>1e-7?z:0.0);

        getMainWindow()->showMessage(QString::fromLatin1(buf));

        int ret = Gui::Selection().setPreselect(docname,objname,element,x,y,z);
        if(ret<0 && currenthighlight)
            return true;

        if(ret) {
            if (currenthighlight) {
                SoHighlightElementAction action;
                action.setHighlighted(false);
                action.apply(currenthighlight);
                currenthighlight->unref();
                currenthighlight = 0;
            }
            currenthighlight = static_cast<SoFullPath*>(path->copy());
            currenthighlight->ref();
            highlighted = true;
        }
    }

    if(!highlighted)
        this->preSelection = 0;

    if(currenthighlight) {
        SoHighlightElementAction action;
        action.setHighlighted(highlighted);
        action.setColor(this->colorHighlight.getValue());
        action.setElement(det);
        action.apply(currenthighlight);
        if(!highlighted) {
            currenthighlight->unref();
            currenthighlight = 0;
            Selection().rmvPreselect();
        }
        this->touch();
    }

    return highlighted;
}

bool SoFCUnifiedSelection::setSelection(const std::vector<PickedInfo> &infos, bool ctrlDown, bool shiftDown) {
    if(infos.empty() || !infos[0].vpd) return false;

    std::vector<SelectionSingleton::SelObj> sels;
    if(infos.size()>1) {
        for(auto &info : infos) {
            if(!info.vpd) continue;
            SelectionSingleton::SelObj sel;
            sel.pObject = info.vpd->getObject();
            sel.pDoc = sel.pObject->getDocument();
            sel.DocName = sel.pDoc->getName();
            sel.FeatName = sel.pObject->getNameInDocument();
            sel.TypeName = sel.pObject->getTypeId().getName();
            sel.SubName = info.element.c_str();
            const auto &pt = info.pp->getPoint();
            sel.x = pt[0];
            sel.y = pt[1];
            sel.z = pt[2];
            sels.push_back(sel);
        }
    }

    const auto &info = infos[0];
    auto vpd = info.vpd;
    if(!vpd) return false;
    const char *objname = vpd->getObject()->getNameInDocument();
    if(!objname) return false;
    const char *docname = vpd->getObject()->getDocument()->getName();

    bool hasNext = false;
    const SoPickedPoint * pp = info.pp;
    const SoDetail *det = pp->getDetail();
    SoDetail *detNext = 0;
    SoFullPath *pPath = static_cast<SoFullPath*>(pp->getPath());
    const auto &pt = pp->getPoint();
    SoSelectionElementAction::Type type = SoSelectionElementAction::None;
    HighlightModes mymode = (HighlightModes) this->highlightMode.getValue();
    static char buf[513];

    if (ctrlDown && !shiftDown) {
        if(Gui::Selection().isSelected(docname,objname,info.element.c_str(),0))
            Gui::Selection().rmvSelection(docname,objname,info.element.c_str(),&sels);
        else {
            bool ok = Gui::Selection().addSelection(docname,objname,
                    info.element.c_str(), pt[0] ,pt[1] ,pt[2], &sels);
            if (ok && mymode == OFF) {
                snprintf(buf,512,"Selected: %s.%s.%s (%g, %g, %g)",
                        docname,objname,info.element.c_str()
                        ,fabs(pt[0])>1e-7?pt[0]:0.0
                        ,fabs(pt[1])>1e-7?pt[1]:0.0
                        ,fabs(pt[2])>1e-7?pt[2]:0.0);

                getMainWindow()->showMessage(QString::fromLatin1(buf));
            }
        }
        return true;
    }

    // Hierarchy ascending
    //
    // If the clicked subelement is already selected, check if there is an
    // upper hierarchy, and select that hierarchy instead. 
    //
    // For example, let's suppose PickedInfo above reports
    // 'link.link2.box.Face1', and below Selection().getSelectedElement returns
    // 'link.link2.box.', meaning that 'box' is the current selected hierarchy,
    // and the user is clicking the box again.  So we shall go up one level,
    // and select 'link.link2.'
    //

    std::string subName = info.element;
    std::string objectName = objname;

    const char *subSelected = Gui::Selection().getSelectedElement(
                                vpd->getObject(),subName.c_str());

    FC_TRACE("select " << (subSelected?subSelected:"'null'") << ", " << 
            objectName << ", " << subName);
    std::string newElement;
    if(subSelected && ((ctrlDown && shiftDown)
                        || Data::ComplexGeoData::hasElementName(subSelected)))
    {
        newElement = Data::ComplexGeoData::newElementName(subSelected);
        subSelected = newElement.c_str();
        std::string nextsub;
        const char *next = strrchr(subSelected,'.');
        if(next && next!=subSelected) {
            if(next[1]==0) {
                // The convention of dot separated SubName demands a mandatory
                // ending dot for every object name reference inside SubName.
                // The non-object sub-element, however, must not end with a dot.
                // So, next[1]==0 here means current selection is a whole object
                // selection (because no sub-element), so we shall search
                // upwards for the second last dot, which is the end of the
                // parent name of the current selected object
                for(--next;next!=subSelected;--next) {
                    if(*next == '.') break;
                }
            }
            if(*next == '.')
                nextsub = std::string(subSelected,next-subSelected+1);
        }
        if(nextsub.length() || *subSelected!=0) {
            hasNext = true;
            subName = nextsub;
            detailPath->truncate(0);
            if(vpd->getDetailPath(subName.c_str(),detailPath,true,detNext) && 
               detailPath->getLength()) 
            {
                pPath = detailPath;
                det = detNext;
                FC_TRACE("select next " << objectName << ", " << subName);
            }
        }
    }

#if 0 // ViewProviderDocumentObject now has default implementation of getElementPicked

    // If no next hierarchy is found, do another try on view provider hierarchies, 
    // which is used by geo feature group.
    if(!hasNext) {
        bool found = false;
        auto vps = this->pcDocument->getViewProvidersByPath(pPath);
        for(auto it=vps.begin();it!=vps.end();++it) {
            auto vpdNext = it->first;
            if(Gui::Selection().isSelected(vpdNext->getObject(),"")) {
                found = true;
                continue;
            }
            if(!found || !vpdNext->useNewSelectionModel() || !vpdNext->isSelectable())
                continue;
            hasNext = true;
            vpd = vpdNext;
            det = 0;
            pPath->truncate(it->second+1);
            objectName = vpd->getObject()->getNameInDocument();
            subName = "";
            break;
        }
    }
#endif

    FC_TRACE("clearing selection");
    Gui::Selection().clearSelection();
    FC_TRACE("add selection");
    bool ok = Gui::Selection().addSelection(docname, objectName.c_str() ,subName.c_str(), 
            pt[0] ,pt[1] ,pt[2], &sels);
    if (ok)
        type = hasNext?SoSelectionElementAction::All:SoSelectionElementAction::Append;

    if (mymode == OFF) {
        snprintf(buf,512,"Selected: %s.%s.%s (%g, %g, %g)",
                docname, objectName.c_str() ,subName.c_str()
                ,fabs(pt[0])>1e-7?pt[0]:0.0
                ,fabs(pt[1])>1e-7?pt[1]:0.0
                ,fabs(pt[2])>1e-7?pt[2]:0.0);

        getMainWindow()->showMessage(QString::fromLatin1(buf));
    }

    if (pPath) {
        FC_TRACE("applying action");
        SoSelectionElementAction action(type);
        action.setColor(this->colorSelection.getValue());
        action.setElement(det);
        action.apply(pPath);
        FC_TRACE("applied action");
        this->touch();
    }

    if(detNext) delete detNext;
    return true;
}

// doc from parent
void
SoFCUnifiedSelection::handleEvent(SoHandleEventAction * action)
{
    // If off then don't handle this event
    if (!selectionRole.getValue()) {
        inherited::handleEvent(action);
        return;
    }

    HighlightModes mymode = (HighlightModes) this->highlightMode.getValue();
    const SoEvent * event = action->getEvent();

    // If we don't need to pick for locate highlighting,
    // then just behave as separator and return.
    // NOTE: we still have to pick for ON even though we don't have
    // to re-render, because the app needs to be notified as the mouse
    // goes over locate highlight nodes.
    //if (highlightMode.getValue() == OFF) {
    //    inherited::handleEvent( action );
    //    return;
    //}

    //
    // If this is a mouseMotion event, then check for locate highlighting
    //
    if (event->isOfType(SoLocation2Event::getClassTypeId())) {
        // NOTE: If preselection is off then we do not check for a picked point because otherwise this search may slow
        // down extremely the system on really big data sets. In this case we just check for a picked point if the data
        // set has been selected.
        if (mymode == AUTO || mymode == ON) {
            double delay = ViewParams::instance()->getPreSelectionDelay();

            preselPos = action->getEvent()->getPosition();
            preselViewport = action->getViewportRegion();

            // Rate limit picking action
            if(delay>0.0 && (SbTime::getTimeOfDay()-preselTime).getValue()<delay) {
                if(!preselTimer.isScheduled()) {
                    preselTimer.setInterval(delay);
                    preselTimer.schedule();
                }
            } else {
                onPreselectTimer();
            }
        }
    }
    // mouse press events for (de)selection
    else if (event->isOfType(SoMouseButtonEvent::getClassTypeId()) && 
             selectionMode.getValue() == SoFCUnifiedSelection::ON) {
        const SoMouseButtonEvent* e = static_cast<const SoMouseButtonEvent *>(event);
        if (SoMouseButtonEvent::isButtonReleaseEvent(e,SoMouseButtonEvent::BUTTON1)) {
            // check to see if the mouse is over a geometry...
            auto infos = this->getPickedList(action,!Selection().needPickedList());
            if(setSelection(infos,event->wasCtrlDown(),event->wasShiftDown()))
                action->setHandled();
        } // mouse release
    }

    inherited::handleEvent(action);
}

void SoFCUnifiedSelection::GLRenderBelowPath(SoGLRenderAction * action)
{
    inherited::GLRenderBelowPath(action);

    // nothing picked, so restore the arrow cursor if needed
    if (this->preSelection == 0) {
        // this is called when a selection gate forbade to select an object
        // and the user moved the mouse to an empty area
        this->preSelection = -1;
        QtGLWidget* window;
        SoState *state = action->getState();
        SoGLWidgetElement::get(state, window);
        QWidget* parent = window ? window->parentWidget() : 0;
        if (parent) {
            QCursor c = parent->cursor();
            if (c.shape() == Qt::ForbiddenCursor) {
                c.setShape(Qt::ArrowCursor);
                parent->setCursor(c);
            }
        }
    }
}

// ---------------------------------------------------------------

SO_ACTION_SOURCE(SoHighlightElementAction)

void SoHighlightElementAction::initClass()
{
    SO_ACTION_INIT_CLASS(SoHighlightElementAction,SoAction);

    SO_ENABLE(SoHighlightElementAction, SoSwitchElement);

    SO_ACTION_ADD_METHOD(SoNode,nullAction);

    SO_ENABLE(SoHighlightElementAction, SoCoordinateElement);

    SO_ACTION_ADD_METHOD(SoGroup,callDoAction);
    SO_ACTION_ADD_METHOD(SoIndexedLineSet,callDoAction);
    SO_ACTION_ADD_METHOD(SoIndexedFaceSet,callDoAction);
    SO_ACTION_ADD_METHOD(SoPointSet,callDoAction);
}

SoHighlightElementAction::SoHighlightElementAction () : _highlight(false), _det(0)
{
    SO_ACTION_CONSTRUCTOR(SoHighlightElementAction);
}

SoHighlightElementAction::~SoHighlightElementAction()
{
}

void SoHighlightElementAction::beginTraversal(SoNode *node)
{
    traverse(node);
}

void SoHighlightElementAction::callDoAction(SoAction *action,SoNode *node)
{
    node->doAction(action);
}

void SoHighlightElementAction::setHighlighted(SbBool ok)
{
    this->_highlight = ok;
}

SbBool SoHighlightElementAction::isHighlighted() const
{
    return this->_highlight;
}

void SoHighlightElementAction::setColor(const SbColor& c)
{
    this->_color = c;
}

const SbColor& SoHighlightElementAction::getColor() const
{
    return this->_color;
}

void SoHighlightElementAction::setElement(const SoDetail* det)
{
    this->_det = det;
}

const SoDetail* SoHighlightElementAction::getElement() const
{
    return this->_det;
}

// ---------------------------------------------------------------

SO_ACTION_SOURCE(SoSelectionElementAction)

void SoSelectionElementAction::initClass()
{
    SO_ACTION_INIT_CLASS(SoSelectionElementAction,SoAction);

    SO_ENABLE(SoSelectionElementAction, SoSwitchElement);

    SO_ACTION_ADD_METHOD(SoNode,nullAction);

    SO_ENABLE(SoSelectionElementAction, SoCoordinateElement);

    SO_ACTION_ADD_METHOD(SoCoordinate3,callDoAction);
    SO_ACTION_ADD_METHOD(SoGroup,callDoAction);
    SO_ACTION_ADD_METHOD(SoIndexedLineSet,callDoAction);
    SO_ACTION_ADD_METHOD(SoIndexedFaceSet,callDoAction);
    SO_ACTION_ADD_METHOD(SoPointSet,callDoAction);
}

SoSelectionElementAction::SoSelectionElementAction (Type t, bool secondary)
    : _type(t), _det(0), _secondary(secondary)
{
    SO_ACTION_CONSTRUCTOR(SoSelectionElementAction);
}

SoSelectionElementAction::~SoSelectionElementAction()
{
}

void SoSelectionElementAction::beginTraversal(SoNode *node)
{
    traverse(node);
}

void SoSelectionElementAction::callDoAction(SoAction *action,SoNode *node)
{
    node->doAction(action);
}

SoSelectionElementAction::Type
SoSelectionElementAction::getType() const
{
    return this->_type;
}

void SoSelectionElementAction::setColor(const SbColor& c)
{
    this->_color = c;
}

const SbColor& SoSelectionElementAction::getColor() const
{
    return this->_color;
}

void SoSelectionElementAction::setElement(const SoDetail* det)
{
    this->_det = det;
}

const SoDetail* SoSelectionElementAction::getElement() const
{
    return this->_det;
}

// ---------------------------------------------------------------

SO_ACTION_SOURCE(SoVRMLAction)

void SoVRMLAction::initClass()
{
    SO_ACTION_INIT_CLASS(SoVRMLAction,SoAction);

    SO_ENABLE(SoVRMLAction, SoSwitchElement);

    SO_ACTION_ADD_METHOD(SoNode,nullAction);

    SO_ENABLE(SoVRMLAction, SoCoordinateElement);
    SO_ENABLE(SoVRMLAction, SoMaterialBindingElement);
    SO_ENABLE(SoVRMLAction, SoLazyElement);
    SO_ENABLE(SoVRMLAction, SoShapeStyleElement);

    SO_ACTION_ADD_METHOD(SoCoordinate3,callDoAction);
    SO_ACTION_ADD_METHOD(SoMaterialBinding,callDoAction);
    SO_ACTION_ADD_METHOD(SoMaterial,callDoAction);
    SO_ACTION_ADD_METHOD(SoNormalBinding,callDoAction);
    SO_ACTION_ADD_METHOD(SoGroup,callDoAction);
    SO_ACTION_ADD_METHOD(SoIndexedLineSet,callDoAction);
    SO_ACTION_ADD_METHOD(SoIndexedFaceSet,callDoAction);
    SO_ACTION_ADD_METHOD(SoPointSet,callDoAction);
}

SoVRMLAction::SoVRMLAction() : overrideMode(true)
{
    SO_ACTION_CONSTRUCTOR(SoVRMLAction);
}

SoVRMLAction::~SoVRMLAction()
{
}

void SoVRMLAction::setOverrideMode(SbBool on)
{
    overrideMode = on;
}

SbBool SoVRMLAction::isOverrideMode() const
{
    return overrideMode;
}

void SoVRMLAction::callDoAction(SoAction *action, SoNode *node)
{
    if (node->getTypeId().isDerivedFrom(SoNormalBinding::getClassTypeId()) && action->isOfType(SoVRMLAction::getClassTypeId())) {
        SoVRMLAction* vrmlAction = static_cast<SoVRMLAction*>(action);
        if (vrmlAction->overrideMode) {
            SoNormalBinding* bind = static_cast<SoNormalBinding*>(node);
            vrmlAction->bindList.push_back(bind->value.getValue());
            // this normal binding causes some problems for the part view provider
            // See also #0002222: Number of normals in exported VRML is wrong
            if (bind->value.getValue() == static_cast<int>(SoNormalBinding::PER_VERTEX_INDEXED))
                bind->value = SoNormalBinding::OVERALL;
        }
        else if (!vrmlAction->bindList.empty()) {
            static_cast<SoNormalBinding*>(node)->value = static_cast<SoNormalBinding::Binding>(vrmlAction->bindList.front());
            vrmlAction->bindList.pop_front();
        }
    }

    node->doAction(action);
}

// ---------------------------------------------------------------------------------

bool SoFCSelectionRoot::StackComp::operator()(const Stack &a, const Stack &b) const {
    if(a.size()-a.offset < b.size()-b.offset) 
        return true;
    if(a.size()-a.offset > b.size()-b.offset) 
        return false;
    auto it1=a.rbegin(), end1=a.rend()-a.offset; 
    auto it2=b.rbegin();
    for(;it1!=end1;++it1,++it2) {
        if(*it1 < *it2) 
            return true;
        if(*it1 > *it2) 
            return false;
    }
    return false;
}

// ---------------------------------------------------------------------------------
SO_NODE_SOURCE(SoFCSwitch)

SoFCSwitch::SoFCSwitch()
{
    SO_NODE_CONSTRUCTOR(SoFCSwitch);
    SO_NODE_ADD_FIELD(defaultChild,  (0));
    SO_NODE_ADD_FIELD(overrideSwitch,(OverrideNone));
    SO_NODE_DEFINE_ENUM_VALUE(OverrideSwitch, OverrideNone);
    SO_NODE_DEFINE_ENUM_VALUE(OverrideSwitch, OverrideDefault);
    SO_NODE_DEFINE_ENUM_VALUE(OverrideSwitch, OverrideVisible);
    SO_NODE_DEFINE_ENUM_VALUE(OverrideSwitch, OverrideReset);
    SO_NODE_SET_SF_ENUM_TYPE(overrideSwitch, OverrideSwitch);
}

// switch to defaultChild when invisible
#define FC_SWITCH_DEFAULT (0x10000000)
#define FC_SWITCH_VISIBLE (0x20000000)
#define FC_SWITCH_RESET   (0x30000000)
#define FC_SWITCH_MASK    (0xF0000000)

void SoFCSwitch::switchOverride(SoAction *action, OverrideSwitch o) {
    if(action) {
        int which;
        switch(o) {
        case OverrideDefault:
            which = FC_SWITCH_DEFAULT;
            break;
        case OverrideVisible:
            which = FC_SWITCH_VISIBLE;
            break;
        default:
            which = SO_SWITCH_NONE;
        }
        SoSwitchElement::set(action->getState(),which);
    }
}

struct SwitchInfo {
    CoinPtr<SoPath> path;
    int idx;

    SwitchInfo(SoPath *p)
        :path(p),idx(-1)
    {
        if(next()<0)
            path.reset();
    }

    int next() {
        if(!path)
            return -1;
        int count = path->getLength();
        if(idx>=count)
            return -1;
        for(++idx;idx<count;++idx) {
            if(path->getNode(idx)->isOfType(SoFCSwitch::getClassTypeId()))
                break;
        }
        return idx<count?idx:-1;
    }
};

static thread_local std::deque<SwitchInfo> _SwitchStack;
static thread_local std::deque<SoFCSwitch::TraverseState> _SwitchTraverseStack;

bool SoFCSwitch::testTraverseState(TraverseStateFlag flag) {
    if(_SwitchTraverseStack.size())
        return _SwitchTraverseStack.back().test((std::size_t)flag);
    return false;
}

void SoFCSwitch::doAction(SoAction *action) {
    auto state = action->getState();

    uint32_t mask = ((uint32_t)SoSwitchElement::get(state)) & FC_SWITCH_MASK;
    int idx = -1;

    switch(overrideSwitch.getValue()) {
    case OverrideDefault:
        if(mask != FC_SWITCH_VISIBLE)
            mask = FC_SWITCH_DEFAULT;
        break;
    default:
        break;
    }

    if((mask!=FC_SWITCH_DEFAULT && mask!=FC_SWITCH_VISIBLE)
            || (action->isOfType(SoCallbackAction::getClassTypeId()) &&
                ((SoCallbackAction *)action)->isCallbackAll()))
    {
        inherited::doAction(action);
        return;
    }

    int numindices = 0;
    const int * indices = 0;
    SoAction::PathCode pathcode = action->getPathCode(numindices, indices);

    if(_SwitchStack.size() && _SwitchStack.back().path) {
        auto &info = _SwitchStack.back();
        if(info.path->getNode(info.idx) == this) {
            // We are traversing inside a path from some parent SoFCPathAnnotation.
            // We shall override the switch index according to the path inside
            if(info.idx+1<info.path->getLength()) 
                idx = info.path->getIndex(info.idx+1);

            int nodeIdx = info.idx;
            if(info.next()<0) {
                if(nodeIdx+1==info.path->getLength())
                    idx = this->defaultChild.getValue();
                // We are the last SoFCSwitch node inside the path, reset the
                // path so we do not override visibility below. We will still
                // override switch if the node is visible.
                info.path.reset();
            }
        }
    } else if (numindices == 1) {
        // this means we applying the action to a path, and we are traversing in the middle of it.
        idx = indices[0];
    } else if (action->getWhatAppliedTo() == SoAction::PATH) {
        auto path = action->getPathAppliedTo();
        if(path && path->getLength() && path->getNodeFromTail(0) == this)
            idx = this->defaultChild.getValue();
    }

    if(idx<0 && idx!=SO_SWITCH_ALL) {
        if((mask==FC_SWITCH_VISIBLE || whichChild.getValue()!=SO_SWITCH_NONE)
                && this->defaultChild.getValue()!=SO_SWITCH_NONE)
        {
            idx = this->defaultChild.getValue();
        } else
            idx = this->whichChild.getValue();
    }

    if(idx!=SO_SWITCH_ALL && (idx<0 || idx>=this->getNumChildren())) {
        inherited::doAction(action);
        return;
    }

    switch(overrideSwitch.getValue()) {
    case OverrideVisible:
        // OverrideVisible is only applicable to children
        mask = FC_SWITCH_VISIBLE;
        break;
    case OverrideReset:
        if(_SwitchStack.empty() || !_SwitchStack.back().path)
            mask = FC_SWITCH_RESET;
        break;
    default:
        break;
    }
    uint32_t uidx = (uint32_t)idx;
    SoSwitchElement::set(state, (int32_t)(mask|(uidx & ~FC_SWITCH_MASK)));

    TraverseState tstate(0);
    if(_SwitchTraverseStack.size()) {
        tstate = _SwitchTraverseStack.back();
        tstate.reset(TraverseAlternative);
    } else
        tstate.set(TraverseOverride);

    if(whichChild.getValue() == SO_SWITCH_NONE)
        tstate.set(TraverseInvisible);
    else if(whichChild.getValue()!=idx) 
        tstate.set(TraverseAlternative);

    if(!_SwitchTraverseStack.size() || _SwitchTraverseStack.back()!=tstate)
        _SwitchTraverseStack.push_back(tstate);
    else
        tstate.reset();

    if(idx == SO_SWITCH_ALL) {
        if (pathcode == SoAction::IN_PATH)
            this->children->traverseInPath(action, numindices, indices);
        else
            this->children->traverse(action);
    } else if (pathcode == SoAction::IN_PATH) {
        // traverse only if one path matches idx
        for (int i = 0; i < numindices; i++) {
            if (indices[i] == idx) {
                this->children->traverse(action, idx);
                break;
            }
        }
    } else
        this->children->traverse(action, idx);

    if(tstate.to_ulong())
        _SwitchTraverseStack.pop_back();
}

void SoFCSwitch::getBoundingBox(SoGetBoundingBoxAction * action)
{
    if(cb)
        cb();
    doAction(action);
}

void SoFCSwitch::search(SoSearchAction * action)
{
    SoNode::search(action);
    if (action->isFound()) return;

    if (action->isSearchingAll()) {
        this->children->traverse(action);
    }
    else {
        doAction(action);
    }
}

void SoFCSwitch::callback(SoCallbackAction *action)
{
    doAction(action);
}

void SoFCSwitch::pick(SoPickAction *action)
{
    doAction((SoAction*)action);
}

void SoFCSwitch::handleEvent(SoHandleEventAction *action)
{
    doAction(action);
}

void SoFCSwitch::initClass(void)
{
    SO_NODE_INIT_CLASS(SoFCSwitch,SoSwitch,"FCSwitch");
}

void SoFCSwitch::finish()
{
    atexit_cleanup();
}


// ---------------------------------------------------------------------------------
SoSeparator::CacheEnabled SoFCSeparator::CacheMode = SoSeparator::AUTO;
SO_NODE_SOURCE(SoFCSeparator)

SoFCSeparator::SoFCSeparator(bool trackCacheMode)
    :trackCacheMode(trackCacheMode)
{
    SO_NODE_CONSTRUCTOR(SoFCSeparator);
    if(!trackCacheMode) {
        renderCaching = SoSeparator::OFF;
        boundingBoxCaching = SoSeparator::OFF;
    }
}

void SoFCSeparator::GLRenderBelowPath(SoGLRenderAction * action) {
    if(trackCacheMode && renderCaching.getValue()!=CacheMode) {
        renderCaching = CacheMode;
        boundingBoxCaching = CacheMode;
    }
    inherited::GLRenderBelowPath(action);
}

void SoFCSeparator::initClass(void)
{
    SO_NODE_INIT_CLASS(SoFCSeparator,SoSeparator,"FCSeparator");
}

void SoFCSeparator::finish()
{
    atexit_cleanup();
}

// ---------------------------------------------------------------------------------
// Thread local data for bounding box rendering
//
// The code is inspred by Coin SoLevelOfDetails.cpp.
typedef struct {
    SoGetBoundingBoxAction * bboxaction;
    SoCube *cube;
} SoFCBBoxRenderInfo;

static void so_bbox_construct_data(void * closure)
{
    SoFCBBoxRenderInfo * data = (SoFCBBoxRenderInfo*) closure;
    data->bboxaction = NULL;
    data->cube = NULL;
}

static void so_bbox_destruct_data(void * closure)
{
    SoFCBBoxRenderInfo * data = (SoFCBBoxRenderInfo*) closure;
    delete data->bboxaction;
    if(data->cube)
        data->cube->unref();
}

static SbStorage * so_bbox_storage = NULL;

// called from atexit
static void so_bbox_cleanup(void)
{
    delete so_bbox_storage;
}

// ---------------------------------------------------------------------------------

SoFCSelectionRoot::Stack SoFCSelectionRoot::SelStack;
std::unordered_map<SoAction*,SoFCSelectionRoot::Stack> SoFCSelectionRoot::ActionStacks;
SoFCSelectionRoot::ColorStack SoFCSelectionRoot::SelColorStack;
SoFCSelectionRoot::ColorStack SoFCSelectionRoot::HlColorStack;
SoFCSelectionRoot* SoFCSelectionRoot::ShapeColorNode;

SO_NODE_SOURCE(SoFCSelectionRoot)

SoFCSelectionRoot::SoFCSelectionRoot(bool trackCacheMode)
    :SoFCSeparator(trackCacheMode)
{
    SO_NODE_CONSTRUCTOR(SoFCSelectionRoot);
    SO_NODE_ADD_FIELD(selectionStyle,(Full));
    SO_NODE_DEFINE_ENUM_VALUE(SelectStyles, Full);
    SO_NODE_DEFINE_ENUM_VALUE(SelectStyles, Box);
    SO_NODE_DEFINE_ENUM_VALUE(SelectStyles, PassThrough);
    SO_NODE_SET_SF_ENUM_TYPE(selectionStyle, SelectStyles);
}

SoFCSelectionRoot::~SoFCSelectionRoot()
{
}

void SoFCSelectionRoot::initClass(void)
{
    SO_NODE_INIT_CLASS(SoFCSelectionRoot,SoFCSeparator,"FCSelectionRoot");

    so_bbox_storage = new SbStorage(sizeof(SoFCBBoxRenderInfo),
            so_bbox_construct_data, so_bbox_destruct_data);

    // cc_coin_atexit((coin_atexit_f*) so_bbox_cleanup);
}

void SoFCSelectionRoot::finish()
{
    so_bbox_cleanup();
    atexit_cleanup();
}

SoNode *SoFCSelectionRoot::getCurrentRoot(bool front, SoNode *def) {
    if(SelStack.size()) 
        return front?SelStack.front():SelStack.back();
    return def;
}

SoNode *SoFCSelectionRoot::getCurrentActionRoot(SoAction *action, bool front, SoNode *def) {
    auto it = ActionStacks.find(action);
    if(it == ActionStacks.end() || it->second.empty())
        return def;
    return front?it->second.front():it->second.back();
}


SoFCSelectionContextBasePtr SoFCSelectionRoot::getNodeContext(
        Stack &stack, SoNode *node, SoFCSelectionContextBasePtr def)
{
    if(stack.empty())
        return def;

    SoFCSelectionRoot *front = stack.front();

    // NOTE: _node is not necssary of type SoFCSelectionRoot, but it is safe
    // here since we only use it as searching key, although it is probably not
    // a best practice.
    stack.front() = static_cast<SoFCSelectionRoot*>(node);

    auto it = front->contextMap.find(stack);
    stack.front() = front;
    if(it!=front->contextMap.end()) 
        return it->second;
    return SoFCSelectionContextBasePtr();
}

SoFCSelectionContextBasePtr 
SoFCSelectionRoot::getNodeContext2(Stack &stack, SoNode *node, SoFCSelectionContextBase::MergeFunc *merge) 
{
    SoFCSelectionContextBasePtr ret;
    if(stack.empty() || stack.back()->contextMap2.empty())
        return ret;

    int status = 0;
    auto *back = stack.back();
    auto &map = back->contextMap2;
    stack.back() = static_cast<SoFCSelectionRoot*>(node);
    for(stack.offset=0;stack.offset<stack.size();++stack.offset) {
        auto it = map.find(stack);
        SoFCSelectionContextBasePtr ctx;
        if(it!=map.end())
            ctx = it->second;
        status = merge(status,ret,ctx,stack.offset==stack.size()-1?0:stack[stack.offset]);
        if(status<0)
            break;
    }
    stack.offset = 0;
    stack.back() = back;
    return ret;
}

std::pair<bool,SoFCSelectionContextBasePtr*> SoFCSelectionRoot::findActionContext(
        SoAction *action, SoNode *_node, bool create, bool erase)
{
    std::pair<bool,SoFCSelectionContextBasePtr*> res(false,0);
    if(action->isOfType(SoSelectionElementAction::getClassTypeId()))
        res.first = static_cast<SoSelectionElementAction*>(action)->isSecondary();

    auto it = ActionStacks.find(action);
    if(it==ActionStacks.end() || it->second.empty())
        return res;

    auto &stack = it->second;
    
    auto node = static_cast<SoFCSelectionRoot*>(_node);

    if(res.first) {
        auto back = stack.back();
        stack.back() = node;
        if(create)
            res.second = &back->contextMap2[stack];
        else {
            auto it = back->contextMap2.find(stack);
            if(it!=back->contextMap2.end()) {
                res.second = &it->second;
                if(erase)
                    back->contextMap2.erase(it);
            }
        }
        stack.back() = back;
    }else{
        auto front = stack.front();
        stack.front() = node;
        if(create) 
            res.second = &front->contextMap[stack];
        else {
            auto it = front->contextMap.find(stack);
            if(it!=front->contextMap.end()) {
                res.second = &it->second;
                if(erase)
                    front->contextMap.erase(it);
            }
        }
        stack.front() = front;
    }
    return res;
}

void SoFCSelectionRoot::setupSelectionLineRendering(
        SoState *state, SoNode *node, const uint32_t &color)
{
    float width = SoLineWidthElement::get(state);
    if(width < 1.0)
        width = 1.0;

    if(Gui::ViewParams::instance()->getSelectionLineThicken()>1.0)
        width *= Gui::ViewParams::instance()->getSelectionLineThicken();

    SoLineWidthElement::set(state,width);

    SoLightModelElement::set(state,SoLightModelElement::BASE_COLOR);
    SoMaterialBindingElement::set(state,SoMaterialBindingElement::OVERALL);

    SoLazyElement::setPacked(state, node, 1, &color, false);
}

bool SoFCSelectionRoot::renderBBox(
        SoGLRenderAction *action, SoNode *node, SbColor color) 
{
    auto data = (SoFCBBoxRenderInfo*) so_bbox_storage->get();
    if (data->bboxaction == NULL) {
        // The viewport region will be replaced every time the action is
        // used, so we can just feed it a dummy here.
        data->bboxaction = new SoGetBoundingBoxAction(SbViewportRegion());
        data->cube = new SoCube;
        data->cube->ref();
    }

    auto state = action->getState();

    if(!action->isRenderingDelayedPaths()
            && ViewParams::instance()->getShowSelectionOnTop())
        return false;

    SbBox3f bbox;
    data->bboxaction->setViewportRegion(action->getViewportRegion());
    SoSwitchElement::set(data->bboxaction->getState(), SoSwitchElement::get(action->getState()));
    data->bboxaction->apply(node);
    bbox = data->bboxaction->getBoundingBox();
    if(bbox.isEmpty())
        return false;

    state->push();

    uint32_t packed = color.getPackedValue(0.0);
    setupSelectionLineRendering(state,node,packed);

    SoDrawStyleElement::set(state,SoDrawStyleElement::LINES);
    SoLineWidthElement::set(state,ViewParams::instance()->getSelectionBBoxLineWidth());

    float x, y, z;
    bbox.getSize(x, y, z);
    data->cube->width  = x+0.001;
    data->cube->height  = y+0.001;
    data->cube->depth = z+0.001;

    SoModelMatrixElement::translateBy(state,node,bbox.getCenter());

    SoMaterialBundle mb(action);
    mb.sendFirst();

    FCDepthFunc guard;
    if(!action->isRenderingDelayedPaths())
        guard.set(GL_LEQUAL);
    data->cube->GLRender(action);

    state->pop();
    return true;
}

static std::time_t _CyclicLastReported;

void SoFCSelectionRoot::renderPrivate(SoGLRenderAction * action, bool inPath) {
    if(ViewParams::instance()->getCoinCycleCheck()
            && !SelStack.nodeSet.insert(this).second) 
    {
        std::time_t t = std::time(0);
        if(_CyclicLastReported < t) {
            _CyclicLastReported = t+5;
            FC_ERR("Cyclic scene graph: " << getName());
        }
        return;
    }
    auto state = action->getState();
    bool pushed = false;
    SelStack.push_back(this);
    if(_renderPrivate(action,inPath,pushed)) {
        if(inPath)
            SoSeparator::GLRenderInPath(action);
        else
            SoSeparator::GLRenderBelowPath(action);
    }
    if(pushed)
        state->pop();
    SelStack.pop_back();
    SelStack.nodeSet.erase(this);
}

bool SoFCSelectionRoot::_renderPrivate(SoGLRenderAction * action, bool inPath, bool &pushed) {

    auto state = action->getState();
    selCounter.checkCache(state,true);

    auto ctx2 = std::static_pointer_cast<SelContext>(getNodeContext2(SelStack,this,SelContext::merge));
    if(ctx2 && ctx2->hideAll)
        return false;

    SelContextPtr ctx = getRenderContext<SelContext>(this);

    int style = selectionStyle.getValue();
    if((style==SoFCSelectionRoot::Box || ViewParams::instance()->getShowSelectionBoundingBox())
       && ctx && !ctx->hideAll && (ctx->selAll || ctx->hlAll)) 
    {
        if (style==SoFCSelectionRoot::PassThrough) {
            style = SoFCSelectionRoot::Box;
        } else {
            if(!SoFCSwitch::testTraverseState(SoFCSwitch::TraverseInvisible)) {
                if(inPath)
                    SoSeparator::GLRenderInPath(action);
                else
                    SoSeparator::GLRenderBelowPath(action);
            }
            renderBBox(action,this,ctx->hlAll?ctx->hlColor:ctx->selColor);
            return false;
        }
    }

    // Here, we are not setting (pre)selection color override here.
    // Instead, we are checking and setting up for any secondary context
    // color override.
    //
    // When the current selection style is full highlight, we should ignore any
    // secondary override. If the style is bounding box, however, we should
    // honour the secondary color override.

    bool colorPushed = false;
    if(!ShapeColorNode && overrideColor && 
        !SoOverrideElement::getDiffuseColorOverride(state) &&
        (style==SoFCSelectionRoot::Box || !ctx || (!ctx->selAll && !ctx->hideAll)))
    {
        ShapeColorNode = this;
        colorPushed = true;
        if(!pushed) {
            pushed = true;
            state->push();
        }
        auto &packer = ShapeColorNode->shapeColorPacker;
        auto &trans = ShapeColorNode->transOverride;
        auto &color = ShapeColorNode->colorOverride;
        if(!SoOverrideElement::getTransparencyOverride(state) && trans) {
            SoLazyElement::setTransparency(state, ShapeColorNode, 1, &trans, &packer);
            SoOverrideElement::setTransparencyOverride(state,ShapeColorNode,true);
        }
        SoLazyElement::setDiffuse(state, ShapeColorNode, 1, &color, &packer);
        SoOverrideElement::setDiffuseColorOverride(state,ShapeColorNode,true);
        SoMaterialBindingElement::set(state, ShapeColorNode, SoMaterialBindingElement::OVERALL);
        SoOverrideElement::setMaterialBindingOverride(state,ShapeColorNode,true);

        SoTextureEnabledElement::set(state,ShapeColorNode,false);
    }

    if(!ctx) {
        if(inPath)
            SoSeparator::GLRenderInPath(action);
        else
            SoSeparator::GLRenderBelowPath(action);
    } else {
        bool selPushed;
        bool hlPushed;
        if((hlPushed = ctx->hlAll)) 
            HlColorStack.push_back(ctx->hlColor);
        if((selPushed = ctx->selAll))
            SelColorStack.push_back(ctx->selColor);

        if(!ViewParams::instance()->getShowSelectionOnTop() 
                && selPushed
                && style != SoFCSelectionRoot::Box)
        {
            if(!pushed) {
                pushed = true;
                state->push();
            }
            // Setting color override here is essential for proper caching
            auto &color = SelColorStack.back();
            SoLazyElement::setEmissive(state, &color);
            SoOverrideElement::setEmissiveColorOverride(state,this,true);
            if (SoLazyElement::getLightModel(state) == SoLazyElement::BASE_COLOR) {
                auto &packer = shapeColorPacker;
                SoLazyElement::setDiffuse(state, this, 1, &color, &packer);
                SoOverrideElement::setDiffuseColorOverride(state,this,true);
                SoMaterialBindingElement::set(state, this, SoMaterialBindingElement::OVERALL);
                SoOverrideElement::setMaterialBindingOverride(state,this,true);
            }
        }

        if(inPath)
            SoSeparator::GLRenderInPath(action);
        else
            SoSeparator::GLRenderBelowPath(action);

        if(selPushed) 
            SelColorStack.pop_back();
        if(hlPushed)
            HlColorStack.pop_back();
    }

    if(colorPushed) 
        ShapeColorNode = 0;

    return false;
}

void SoFCSelectionRoot::GLRenderBelowPath(SoGLRenderAction * action) {
    renderPrivate(action,false);
}

void SoFCSelectionRoot::GLRenderInPath(SoGLRenderAction * action) {
    if(action->getCurPathCode() == SoAction::BELOW_PATH)
        return GLRenderBelowPath(action);
    renderPrivate(action,true);
}

bool SoFCSelectionRoot::checkColorOverride(SoState *state) {
    if(ShapeColorNode) {
        if(!SoOverrideElement::getDiffuseColorOverride(state)) {
            state->push();
            auto &packer = ShapeColorNode->shapeColorPacker;
            auto &trans = ShapeColorNode->transOverride;
            auto &color = ShapeColorNode->colorOverride;
            if(!SoOverrideElement::getTransparencyOverride(state) && trans) {
                SoLazyElement::setTransparency(state, ShapeColorNode, 1, &trans, &packer);
                SoOverrideElement::setTransparencyOverride(state,ShapeColorNode,true);
            }
            SoLazyElement::setDiffuse(state, ShapeColorNode, 1, &color, &packer);
            SoOverrideElement::setDiffuseColorOverride(state,ShapeColorNode,true);
            SoMaterialBindingElement::set(state, ShapeColorNode, SoMaterialBindingElement::OVERALL);
            SoOverrideElement::setMaterialBindingOverride(state,ShapeColorNode,true);

            SoTextureEnabledElement::set(state,ShapeColorNode,false);
            return true;
        }
    }
    return false;
}

void SoFCSelectionRoot::checkSelection(bool &sel, SbColor &selColor, bool &hl, SbColor &hlColor) {
    sel = false;
    hl = false;
    if((sel = !SelColorStack.empty()))
        selColor = SelColorStack.back();
    if((hl = !HlColorStack.empty()))
        hlColor = HlColorStack.back();
}

void SoFCSelectionRoot::resetContext() {
    contextMap.clear();
}

void SoFCSelectionRoot::moveActionStack(SoAction *from, SoAction *to, bool erase) {
    auto it = ActionStacks.find(from);
    if(it == ActionStacks.end())
        return;
    auto &stack = ActionStacks[to];
    assert(stack.empty());
    stack.swap(it->second);
    if(erase)
        ActionStacks.erase(it);
}

#define BEGIN_ACTION \
    auto &stack = ActionStacks[action];\
    if(ViewParams::instance()->getCoinCycleCheck() \
        && !stack.nodeSet.insert(this).second) \
    {\
        std::time_t t = std::time(0);\
        if(_CyclicLastReported < t) {\
            _CyclicLastReported = t+5;\
            FC_ERR("Cyclic scene graph: " << getName());\
        }\
        return;\
    }\
    stack.push_back(this);\
    auto size = stack.size();

#define END_ACTION \
    if(stack.size()!=size || stack.back()!=this)\
        FC_ERR("action stack fault");\
    else {\
        stack.nodeSet.erase(this);\
        stack.pop_back();\
        if(stack.empty())\
            ActionStacks.erase(action);\
    }

void SoFCSelectionRoot::pick(SoPickAction * action) {
    BEGIN_ACTION;
    if(doActionPrivate(stack,action))
        inherited::pick(action);
    END_ACTION;
}

void SoFCSelectionRoot::rayPick(SoRayPickAction * action) {
    BEGIN_ACTION;
    if(doActionPrivate(stack,action))
        inherited::rayPick(action);
    END_ACTION;
}

void SoFCSelectionRoot::handleEvent(SoHandleEventAction * action) {
    BEGIN_ACTION;
    inherited::handleEvent(action);
    END_ACTION;
}

void SoFCSelectionRoot::search(SoSearchAction * action) {
    BEGIN_ACTION;
    inherited::search(action);
    END_ACTION;
}

void SoFCSelectionRoot::getPrimitiveCount(SoGetPrimitiveCountAction * action) {
    BEGIN_ACTION;
    inherited::getPrimitiveCount(action);
    END_ACTION;
}

void SoFCSelectionRoot::getBoundingBox(SoGetBoundingBoxAction * action)
{
    BEGIN_ACTION;
    if(doActionPrivate(stack,action)) {
        selCounter.checkCache(action->getState(),true);
        inherited::getBoundingBox(action);
    }
    END_ACTION;
}

void SoFCSelectionRoot::getMatrix(SoGetMatrixAction * action) {
    BEGIN_ACTION;
    if(doActionPrivate(stack,action))
        inherited::getMatrix(action);
    END_ACTION;
}

void SoFCSelectionRoot::callback(SoCallbackAction *action) {
    BEGIN_ACTION;
    inherited::callback(action);
    END_ACTION;
}

void SoFCSelectionRoot::doAction(SoAction *action) {
    BEGIN_ACTION
    if(doActionPrivate(stack,action))
        inherited::doAction(action);
    END_ACTION
}

bool SoFCSelectionRoot::doActionPrivate(Stack &stack, SoAction *action) {
    // Selection action short-circuit optimization. In case of whole object
    // selection/pre-selection, we shall store a SelContext keyed by ourself.
    // And the action traversal can be short-curcuited once the first targeted
    // SoFCSelectionRoot is found here. New function checkSelection() is exposed
    // to check for whole object selection. This greatly improve performance on
    // large group.

    SelContextPtr ctx2;
    bool ctx2Searched = false;
    bool isTail = false;
    if(action->getCurPathCode()==SoAction::IN_PATH) {
        auto path = action->getPathAppliedTo();
        if(path) {
            isTail = path->getTail()==this || 
                     (path->getLength()>1 
                      && path->getNodeFromTail(1)==this
                      && path->getTail()->isOfType(SoSwitch::getClassTypeId()));
        }

        if(!action->isOfType(SoSelectionElementAction::getClassTypeId())) {
            ctx2Searched = true;
            ctx2 = std::static_pointer_cast<SelContext>(getNodeContext2(stack,this,SelContext::merge));
            if(ctx2 && ctx2->hideAll)
                return false;
        }
        if(!isTail)
            return true;
    }else if(action->getWhatAppliedTo()!=SoAction::NODE && action->getCurPathCode()!=SoAction::BELOW_PATH)
        return true;

    if(action->isOfType(SoSelectionElementAction::getClassTypeId())) {
        auto selAction = static_cast<SoSelectionElementAction*>(action);

        if(selAction->isSecondary()) {
            if(selAction->getType() == SoSelectionElementAction::Show ||
               (selAction->getType() == SoSelectionElementAction::Color && 
                selAction->getColors().empty() &&
                action->getWhatAppliedTo()==SoAction::NODE))
            {
                auto ctx = getActionContext(action,this,SelContextPtr(),false);
                selCounter.checkAction(selAction,ctx);
                if(ctx && ctx->hideAll) {
                    ctx->hideAll = false;
                    if(!ctx->hlAll && !ctx->selAll)
                        removeActionContext(action,this);
                    touch();
                }
                // applied to a node means clear all visibility setting, so
                // return true to propgate the action
                return selAction->getType()==SoSelectionElementAction::Color ||
                       action->getWhatAppliedTo()==SoAction::NODE;

            }else if(selAction->getType() == SoSelectionElementAction::Hide) {
                if(action->getCurPathCode()==SoAction::BELOW_PATH || isTail) {
                    auto ctx = getActionContext(action,this,SelContextPtr());
                    selCounter.checkAction(selAction,ctx);
                    if(ctx && !ctx->hideAll) {
                        ctx->hideAll = true;
                        touch();
                    }
                    return false;
                }
            }
            return true;
        } 
        
        if(selAction->getType() == SoSelectionElementAction::None) {
            if(action->getWhatAppliedTo() == SoAction::NODE) {
                // Here the 'select none' action is applied to a node, and we
                // are the first SoFCSelectionRoot encountered (which means all
                // children stores selection context here, both whole object
                // and element selection), then we can simply perform the
                // action by clearing the selection context here, and save the
                // time for traversing a potentially large amount of children
                // nodes.
                resetContext();
                touch();
                return false;
            }

            auto ctx = getActionContext(action,this,SelContextPtr(),false);
            selCounter.checkAction(selAction,ctx);
            if(ctx && ctx->selAll) {
                ctx->selAll = false;
                touch();
                return false;
            }
        } else if(selAction->getType() == SoSelectionElementAction::All) {
            auto ctx = getActionContext(action,this,SelContextPtr());
            assert(ctx);
            selCounter.checkAction(selAction,ctx);
            ctx->selAll = true;
            ctx->selColor = selAction->getColor();
            touch();
            return false;
        }
        return true;
    }

    if(action->isOfType(SoHighlightElementAction::getClassTypeId())) {
        auto hlAction = static_cast<SoHighlightElementAction*>(action);
        if(hlAction->isHighlighted()) {
            selCounter.checkAction(hlAction);
            if(hlAction->getElement()) {
                auto ctx = getActionContext(action,this,SelContextPtr(),false);
                if(ctx && ctx->hlAll) {
                    ctx->hlAll = false;
                    touch();
                }
            } else {
                auto ctx = getActionContext(action,this,SelContextPtr());
                assert(ctx);
                ctx->hlAll = true;
                ctx->hlColor = hlAction->getColor();
                touch();
                return false;
            }
        } else {
            auto ctx = getActionContext(action,this,SelContextPtr(),false);
            if(ctx && ctx->hlAll) {
                ctx->hlAll = false;
                touch();
                return false;
            }
        }
        return true;
    }

    if(!ctx2Searched) {
        ctx2 = std::static_pointer_cast<SelContext>(getNodeContext2(stack,this,SelContext::merge));
        if(ctx2 && ctx2->hideAll)
            return false;
    }
    return true;
}

int SoFCSelectionRoot::SelContext::merge(int status, SoFCSelectionContextBasePtr &output, 
        SoFCSelectionContextBasePtr input, SoFCSelectionRoot *)
{
    auto ctx = std::dynamic_pointer_cast<SelContext>(input);
    if(ctx && ctx->hideAll) {
        output = ctx;
        return -1;
    }
    return status;
}

/////////////////////////////////////////////////////////////////////////////

FCDepthFunc::FCDepthFunc(int32_t f)
    :func(0),changed(false),dtest(false)
{
    set(f);
}

FCDepthFunc::FCDepthFunc()
    :func(0),changed(false),dtest(false)
{}

FCDepthFunc::~FCDepthFunc() {
    restore();
}

void FCDepthFunc::restore() {
    if(func && changed) {
        changed = false;
        glDepthFunc(func);
    }
    if(dtest) {
        dtest = false;
        glDisable(GL_DEPTH_TEST);
    }
}
    
void FCDepthFunc::set(int32_t f) {
    int32_t oldFunc;
    glGetIntegerv(GL_DEPTH_FUNC, &oldFunc);
    if(!func)
        func = oldFunc;
    if(oldFunc!=f) {
        changed = true;
        glDepthFunc(f);
    }
    if(!dtest && !glIsEnabled(GL_DEPTH_TEST)) {
        dtest = true;
        glEnable(GL_DEPTH_TEST);
    }
}

/////////////////////////////////////////////////////////////////////////////

SO_NODE_SOURCE(SoFCPathAnnotation)

SoFCPathAnnotation::SoFCPathAnnotation()
{
    SO_NODE_CONSTRUCTOR(SoFCPathAnnotation);
    path = 0;
    tmpPath = 0;
    det = false;
    this->renderCaching = SoSeparator::OFF;
    this->boundingBoxCaching = SoSeparator::OFF;
}

SoFCPathAnnotation::~SoFCPathAnnotation()
{
    if(path) path->unref();
    if(tmpPath) tmpPath->unref();
}

void SoFCPathAnnotation::finish() 
{
    atexit_cleanup();
}

void SoFCPathAnnotation::initClass(void)
{
    SO_NODE_INIT_CLASS(SoFCPathAnnotation,SoSeparator,"Separator");
}

void SoFCPathAnnotation::GLRender(SoGLRenderAction * action)
{
    switch (action->getCurPathCode()) {
    case SoAction::NO_PATH:
    case SoAction::BELOW_PATH:
        this->GLRenderBelowPath(action);
        break;
    case SoAction::OFF_PATH:
        break;
    case SoAction::IN_PATH:
        this->GLRenderInPath(action);
        break;
    }
}

void SoFCPathAnnotation::GLRenderBelowPath(SoGLRenderAction * action)
{
    if(!path || !path->getLength() || !tmpPath || !tmpPath->getLength())
        return;

    if(path->getLength() != tmpPath->getLength()) {
        // The auditing SoPath may be truncated due to harmless things such as
        // fliping a SoSwitch sibling node. So we keep an unauditing SoTempPath
        // around to try to restore the path.
        for(int i=path->getLength()-1;i<tmpPath->getLength()-1;++i) {
            auto children = path->getNode(i)->getChildren();
            if(children) {
                int idx = children->find(tmpPath->getNode(i+1));
                if(idx >= 0) {
                    path->append(idx);
                    continue;
                }
            }
            tmpPath->unref();
            tmpPath = 0;
            return;
        }
    }

    if (action->isRenderingDelayedPaths()) {
        GLboolean dtest = glIsEnabled(GL_DEPTH_TEST);
        if(dtest)
            glDisable(GL_DEPTH_TEST);

        // SoFCSelectionRoot will trigger switching override for all lower
        // hierarchy SoFCSwitch nodes. This means all lower hierarchy
        // children will made visible. This could cause slow down in
        // rendering. Our goal here is to only override switches within the
        // configured path, and turn off visibility override below the path
        _SwitchStack.emplace_back(path);

        if(det)
            inherited::GLRenderInPath(action);
        else {
            bool bbox = ViewParams::instance()->getShowSelectionBoundingBox();
            if(!bbox) {
                for(int i=0,count=path->getLength();i<count;++i) {
                    if(!path->getNode(i)->isOfType(SoFCSelectionRoot::getClassTypeId()))
                        continue;
                    auto node = static_cast<SoFCSelectionRoot*>(path->getNode(i));
                    if(node->selectionStyle.getValue()==SoFCSelectionRoot::Box) {
                        bbox = true;
                        break;
                    }
                }
            }

            if(!bbox)
                inherited::GLRenderInPath(action);
            else {
                bool sel = false;
                bool hl = false;
                float trans;
                SbColor selColor,hlColor;
                SoFCSelectionRoot::checkSelection(sel,selColor,hl,hlColor);
                if(!sel && !hl)
                    selColor.setPackedValue(ViewParams::instance()->getSelectionColor(),trans);
                SoFCSelectionRoot::renderBBox(action,this,hl?hlColor:selColor);
            }
        }

        _SwitchStack.pop_back();
        if(dtest)
            glEnable(GL_DEPTH_TEST);

    } else {
        auto curPath = action->getCurPath();
        SoPath *newPath = new SoPath(curPath->getLength()+path->getLength());
        newPath->append(curPath);
        newPath->append(path);
        action->addDelayedPath(newPath);
    }
}

void SoFCPathAnnotation::GLRenderInPath(SoGLRenderAction * action)
{
    GLRenderBelowPath(action);
}

void SoFCPathAnnotation::setDetail(bool d) {
    det = d;
}

void SoFCPathAnnotation::setPath(SoPath *newPath) {
    if(path) {
        path->unref();
        coinRemoveAllChildren(this);
        path = 0;
        if(tmpPath) {
            tmpPath->unref();
            tmpPath = 0;
        }
    }
    if(!newPath || !newPath->getLength())
        return;

    tmpPath = new SoTempPath(newPath->getLength());
    tmpPath->ref();
    for(int i=0;i<newPath->getLength();++i)
        tmpPath->append(newPath->getNode(i));
    path = newPath->copy();
    path->ref();
    addChild(path->getNode(0));
}

void SoFCPathAnnotation::getBoundingBox(SoGetBoundingBoxAction * action)
{
    if(path) {
        _SwitchStack.emplace_back(path);
        // TODO: it is better to use SbStorage
        static thread_local SoGetBoundingBoxAction *bboxAction = 0;
        if(!bboxAction)
            bboxAction = new SoGetBoundingBoxAction(SbViewportRegion());
        bboxAction->setViewportRegion(action->getViewportRegion());
        SoFCSelectionRoot::moveActionStack(action,bboxAction,false);
        SoSwitchElement::set(bboxAction->getState(),SoSwitchElement::get(action->getState()));
        bboxAction->apply(path);
        SoFCSelectionRoot::moveActionStack(bboxAction,action,true);
        auto bbox = bboxAction->getBoundingBox();
        if(!bbox.isEmpty())
            action->extendBy(bbox);
        _SwitchStack.pop_back();
    }
}

void SoFCPathAnnotation::doPick(SoPath *curPath, SoRayPickAction *action) {
    if(path) {
        _SwitchStack.emplace_back(path);
        int length = curPath->getLength();
        curPath->append(this);
        curPath->append(path);
        action->apply(curPath);
        curPath->truncate(length);
        _SwitchStack.pop_back();
    }
}
void SoFCPathAnnotation::doAction(SoAction *action) {
    if(path)
        _SwitchStack.emplace_back(path);
    inherited::doAction(action);
    if(path)
        _SwitchStack.pop_back();
}
