/***************************************************************************
 *   Copyright (c) Jürgen Riegel          (juergen.riegel@web.de) 2002     *
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
# include <cstdlib>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <Base/Writer.h>
#include <Base/Reader.h>
#include <Base/Exception.h>
#include <Base/Console.h>
#include "ComplexGeoData.h"

FC_LOG_LEVEL_INIT("ComplexGeoData", true,true);

using namespace Data;

namespace Data {
typedef boost::bimap<
            boost::bimaps::set_of<std::string>,
            boost::bimaps::multiset_of<std::string>,
            boost::bimaps::with_info<std::vector<App::StringIDRef> > > ElementMapBase;
class ElementMap: public ElementMapBase {};
}

TYPESYSTEM_SOURCE_ABSTRACT(Data::Segment , Base::BaseClass);


TYPESYSTEM_SOURCE_ABSTRACT(Data::ComplexGeoData , Base::Persistence);


ComplexGeoData::ComplexGeoData(void)
{
}

ComplexGeoData::~ComplexGeoData(void)
{
}

Data::Segment* ComplexGeoData::getSubElementByName(const char* name) const
{
    int index = 0;
    std::string element(name);
    std::string::size_type pos = element.find_first_of("0123456789");
    if (pos != std::string::npos) {
        index = std::atoi(element.substr(pos).c_str());
        element = element.substr(0,pos);
    }

    return getSubElement(element.c_str(),index);
}

void ComplexGeoData::applyTransform(const Base::Matrix4D& rclTrf)
{
    setTransform(rclTrf * getTransform());
}

void ComplexGeoData::applyTranslation(const Base::Vector3d& mov)
{
    Base::Matrix4D mat;
    mat.move(mov);
    setTransform(mat * getTransform());
}

void ComplexGeoData::applyRotation(const Base::Rotation& rot)
{
    Base::Matrix4D mat;
    rot.getValue(mat);
    setTransform(mat * getTransform());
}

void ComplexGeoData::setPlacement(const Base::Placement& rclPlacement)
{
    setTransform(rclPlacement.toMatrix());
}

Base::Placement ComplexGeoData::getPlacement() const
{
    Base::Matrix4D mat = getTransform();

    return Base::Placement(Base::Vector3d(mat[0][3],
                                          mat[1][3],
                                          mat[2][3]),
                           Base::Rotation(mat));
}

void ComplexGeoData::getLinesFromSubelement(const Segment*,
                                            std::vector<Base::Vector3d> &Points,
                                            std::vector<Line> &lines) const
{
    (void)Points;
    (void)lines;
}

void ComplexGeoData::getFacesFromSubelement(const Segment*,
                                            std::vector<Base::Vector3d> &Points,
                                            std::vector<Base::Vector3d> &PointNormals,
                                            std::vector<Facet> &faces) const
{
    (void)Points;
    (void)PointNormals;
    (void)faces;
}

Base::Vector3d ComplexGeoData::getPointFromLineIntersection(const Base::Vector3f& base,
                                                            const Base::Vector3f& dir) const
{
    (void)base;
    (void)dir;
    return Base::Vector3d();
}

void ComplexGeoData::getPoints(std::vector<Base::Vector3d> &Points,
                               std::vector<Base::Vector3d> &Normals,
                               float Accuracy, uint16_t flags) const
{
    (void)Points;
    (void)Normals;
    (void)Accuracy;
    (void)flags;
}

void ComplexGeoData::getLines(std::vector<Base::Vector3d> &Points,
                              std::vector<Line> &lines,
                              float Accuracy, uint16_t flags) const
{
    (void)Points;
    (void)lines;
    (void)Accuracy;
    (void)flags;
}

void ComplexGeoData::getFaces(std::vector<Base::Vector3d> &Points,
                              std::vector<Facet> &faces,
                              float Accuracy, uint16_t flags) const
{
    (void)Points;
    (void)faces;
    (void)Accuracy;
    (void)flags;
}

bool ComplexGeoData::getCenterOfGravity(Base::Vector3d&) const
{
    return false;
}

const std::string &ComplexGeoData::elementMapPrefix() {
    static std::string prefix(";");
    return prefix;
}

const char *ComplexGeoData::isMappedElement(const char *name) {
    if(name && boost::starts_with(name,elementMapPrefix()))
        return name+elementMapPrefix().size();
    return 0;
}

std::string ComplexGeoData::getElementMapVersion() const {
    std::ostringstream ss;
    ss << 2;
    if(Hasher) 
        ss << '.' << (Hasher->getThreshold()>0?Hasher->getThreshold():0);
    return ss.str();
}

std::string ComplexGeoData::newElementName(const char *name) {
    if(!name) return std::string();
    const char *dot = strrchr(name,'.');
    if(!dot || dot==name) return name;
    const char *c = dot-1;
    for(;c!=name;--c) {
        if(*c == '.') {
            ++c;
            break;
        }
    }
    if(isMappedElement(c))
        return std::string(name,dot);
    return name;
}

size_t ComplexGeoData::getElementMapSize() const {
    return _ElementMap?_ElementMap->size():0;
}

const char *ComplexGeoData::getElementName(const char *name, bool reverse, 
        std::vector<App::StringIDRef> *sid) const 
{
    if(!name || !_ElementMap) 
        return name;

    if(reverse) {
        auto it = _ElementMap->right.find(name);
        if(it == _ElementMap->right.end())
            return name;
        if(sid) sid->insert(sid->end(),it->info.begin(),it->info.end());
        return it->second.c_str();
    }
    const char *txt = isMappedElement(name);
    if(!txt) 
        return name;
    std::string _txt;
    // Strip out the trailing '.XXXX' if any
    const char *dot = strchr(txt,'.');
    if(dot) {
        _txt = std::string(txt,dot-txt);
        txt = _txt.c_str();
    }
    auto it = _ElementMap->left.find(txt);
    if(it == _ElementMap->left.end())
        return name;
    if(sid) sid->insert(sid->end(),it->info.begin(),it->info.end());
    return it->second.c_str();
}

std::vector<std::pair<std::string, std::vector<App::StringIDRef> > >
ComplexGeoData::getElementMappedNames(const char *element, bool needUnmapped) const {
    std::vector<std::pair<std::string, std::vector<App::StringIDRef> > > names;
    if(_ElementMap) {
        auto ret = _ElementMap->right.equal_range(element);
        size_t count=0;
        for(auto it=ret.first;it!=ret.second;++it)
            ++count;
        if(count) {
            names.reserve(count);
            for(auto it=ret.first;it!=ret.second;++it)
                names.emplace_back(it->second,it->info);
            return names;
        }
    }
    if(needUnmapped)
        names.emplace_back(element,std::vector<App::StringIDRef>());
    return names;
}

std::vector<std::pair<std::string,std::string> > 
ComplexGeoData::getElementNamesWithPrefix(const char *prefix) const {
    std::vector<std::pair<std::string,std::string> > names;
    if(!prefix || !prefix[0] || !_ElementMap)
        return names;
    const auto &p = elementMapPrefix();
    if(boost::starts_with(prefix,p))
        prefix += p.size();
    for(auto it=_ElementMap->left.lower_bound(prefix);it!=_ElementMap->left.end();++it) {
        if(boost::starts_with(it->first,prefix))
            names.emplace_back(it->first,it->second);
    }
    return names;
}

std::map<std::string, std::string> ComplexGeoData::getElementMap() const {
    std::map<std::string, std::string> ret;
    if(!_ElementMap) return ret;
    for(auto &v : _ElementMap->left)
        ret.emplace_hint(ret.cend(),v.first,v.second);
    return ret;
}

void ComplexGeoData::setElementMap(const std::map<std::string, std::string> &map) {
    if(!_ElementMap)
        _ElementMap = std::make_shared<ElementMap>();
    else
        _ElementMap->clear();
    for(auto &v : map)
        setElementName(v.first.c_str(),v.second.c_str());
}

void ComplexGeoData::copyElementMap(const ComplexGeoData &data, const char *prefix, const char *postfix) {
    _ElementMap.reset();
    if(!data._ElementMap)
        return;

    if(prefix && !prefix[0])
        prefix = 0;
    if(postfix && !postfix[0])
        postfix = 0;

    if(!Hasher)
        Hasher = data.Hasher;

    for(const auto &v : data._ElementMap->left) {
        if(v.info.size()) {
            if(Hasher!=data.Hasher){
                // different hasher, do not double hash by merging the name into prefix
                std::string name;
                if(prefix)
                    name = prefix;
                name += v.first;
                setElementName(v.second.c_str(), "", name.c_str(), postfix);
            }else
                setElementName(v.second.c_str(), v.first.c_str(), prefix, postfix, &v.info);
        }else
            setElementName(v.second.c_str(), v.first.c_str(), prefix, postfix);
    }
}

const char *ComplexGeoData::setElementName(const char *element, const char *name, 
        const char *prefix, const char *postfix, 
        const std::vector<App::StringIDRef> *sid, bool overwrite)
{
    if(!element || !element[0] || !name || (!prefix&&!postfix))
        return setElementName(element,name,sid,overwrite);

    std::vector<App::StringIDRef> _sid;
    std::ostringstream ss;
    if(prefix)
        ss << prefix;
    if((!sid || sid->empty()) && Hasher) {
        sid = &_sid;
        ss << hashElementName(element[0],name,_sid);
        name = "";
    }else
        ss << name;
    if(postfix)
        ss << postfix;
    if(!Hasher || name[0])
        return setElementName(element,ss.str().c_str(),sid,overwrite);
    else {
        // Have hasher and name empty, meaning only prefix and/or postfix.
        // So we temparorily swap out hasher to prevent hashing prefix/postfix.
        App::StringHasherRef hasher;
        hasher.swap(Hasher);
        auto ret = setElementName(element,ss.str().c_str(),sid,overwrite);
        Hasher.reset(hasher);
        return ret;
    }
}

std::string ComplexGeoData::hashElementName(
        char type, const char *name, std::vector<App::StringIDRef> &sid) 
{
    if(!name)
        throw Base::ValueError("invalid element name");
    if(!Hasher)
        return name;
#ifndef HASH_REDUCE
    sid.push_back(Hasher->getID(name));
    std::string ret("#");
    ret += type;
    ret += std::to_string(sid.back()->value());
    return ret;

    // hash reduce below can reduce hash table size, but not the overall
    // element map size.
#else
    std::string ret,prefix;
    auto pos = strchr(name,'_');
    if(pos) {
        prefix = std::string(name,pos-name+1);
        name = pos+1;
    }
    pos = strstr(name,elementMapPrefix().c_str());
    if(pos) {
        prefix += pos;
        ret = std::string(name,pos-name);
        name = ret.c_str();
    }
    bool hash = true;
    if(name[0] == '#' && (name[1]=='E' || name[1]=='F' || name[1]=='V')) {
        const char *n;
        for(n=name+2;*n && std::isxdigit((unsigned char)*n); ++n);
        if(*n==':') {
            auto hex = n;
            for(++n;*n && std::isxdigit((unsigned char)*n); ++n);
            if(*n==0 && prefix.size()) {
                prefix += hex;
                ret = std::string(name,hex-name);
                name = ret.c_str();
            }
        }
        if(*n==0) {
            hash = false;
            if(name!=ret.c_str())
                ret = name;
            ret[1] = type;
            name = ret.c_str();
        }
    }
    std::ostringstream ss;
    ss << std::hex;
    if(hash) {
        sid.push_back(Hasher->getID(name));
        ss.str("");
        ss << '#' << type << sid.back()->value();
        ret = ss.str();
        name = ret.c_str();
    }
    if(prefix.size()) {
        sid.push_back(Hasher->getID(prefix.c_str()));
        ss.str("");
        ss << name << ':' << sid.back()->value();
        ret = ss.str();
        name = ret.c_str();
    }
    if(name != ret.c_str())
        ret = name;
    return ret;
#endif
}

const char *ComplexGeoData::setElementName(const char *element, const char *name, 
        const std::vector<App::StringIDRef> *sid, bool overwrite)
{
    if(!element || !element[0])
        throw Base::ValueError("Invalid input");
    if(!name || !name[0])  {
        _ElementMap->right.erase(element);
        return element;
    }
    std::vector<App::StringIDRef> _sid;
    const char *mapped = isMappedElement(name);
    if(mapped)
        name = mapped;
    if(!_ElementMap) _ElementMap = std::make_shared<ElementMap>();
    std::string _name;
    if((!sid||sid->empty()) && Hasher) {
        sid = &_sid;
        _name = hashElementName(element[0],name,_sid);
        name = _name.c_str();
    }else if(!sid)
        sid = &_sid;
    auto ret = _ElementMap->left.insert(ElementMap::left_map::value_type(name,element,*sid));
    if(!ret.second && ret.first->second!=element) {
        if(overwrite) {
            _ElementMap->left.erase(ret.first);
            ret = _ElementMap->left.insert(ElementMap::left_map::value_type(name,element,*sid));
        }else {
            std::ostringstream ss;
            ss << "duplicate element mapping '" << name << "->" << element << '/' << ret.first->second;
            throw Base::ValueError(ss.str().c_str());
        }
    }
    FC_TRACE(element << " -> " << name);
    return ret.first->first.c_str();
}

void ComplexGeoData::Save(Base::Writer &writer) const {
    writer.Stream() << writer.ind() << "<ElementMap";
    if(!_ElementMap || _ElementMap->empty())
        writer.Stream() << "/>" << std::endl;
    else {
        writer.Stream() << " count=\"" << _ElementMap->left.size() << "\">" << std::endl;
        for(auto &v : _ElementMap->left) {
            // We are omitting indentation here to save some space in case of long list of elements
            writer.Stream() << "<Element key=\"" <<  
                v.first <<"\" value=\"" << v.second;
            if(v.info.size()) {
                writer.Stream() << "\" sid=\"" << v.info.front()->value();
                for(size_t i=1;i<v.info.size();++i)
                    writer.Stream() << '.' << v.info[i]->value();
            }
            writer.Stream() << "\"/>" << std::endl;
        }
        writer.Stream() << writer.ind() << "</ElementMap>" << std::endl ;
    }
}

void ComplexGeoData::Restore(Base::XMLReader &reader) {
    resetElementMap();
    reader.readElement("ElementMap");
    if(reader.hasAttribute("count")) {
        size_t count = reader.getAttributeAsUnsigned("count");
        for(size_t i=0;i<count;++i) {
            reader.readElement("Element");
            std::vector<App::StringIDRef> sids;
            if(reader.hasAttribute("sid")) {
                if(!Hasher) 
                    FC_ERR("missing hasher");
                else {
                    std::istringstream iss(reader.getAttribute("sid"));
                    long id;
                    while((iss >> id)) {
                        auto sid = Hasher->getID(id);
                        if(!sid) 
                            FC_ERR("invalid string id " << id);
                        else
                            sids.push_back(sid);
                        char sep;
                        iss >> sep;
                    }
                }
            }
            setElementName(reader.getAttribute("value"),"",reader.getAttribute("key"),0,&sids);
        }
        reader.readEndElement("ElementMap");
    }
}

unsigned int ComplexGeoData::getMemSize(void) const {
    if(_ElementMap)
        return _ElementMap->size()*10;
    return 0;
}
