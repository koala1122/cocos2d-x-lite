/****************************************************************************
Copyright (c) 2011      Максим Аксенов
Copyright (c) 2009-2010 Ricardo Quesada
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2011      Zynga Inc.
Copyright (c) 2013-2016 Chukong Technologies Inc.

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#include "2d/CCTMXXMLParser.h"
#include <unordered_map>
#include <sstream>
#include "2d/CCTMXTiledMap.h"
#include "base/ZipUtils.h"
#include "base/base64.h"
#include "base/CCDirector.h"
#include "platform/CCFileUtils.h"

using namespace std;

NS_CC_BEGIN

// implementation TMXLayerInfo
TMXLayerInfo::TMXLayerInfo()
: _name("")
, _tiles(nullptr)
, _ownTiles(true)
{
}

TMXLayerInfo::~TMXLayerInfo()
{
    CCLOGINFO("deallocing TMXLayerInfo: %p", this);
    if (_ownTiles && _tiles)
    {
        free(_tiles);
        _tiles = nullptr;
    }
}

ValueMap& TMXLayerInfo::getProperties()
{
    return _properties;
}

void TMXLayerInfo::setProperties(ValueMap var)
{
    _properties = var;
}

TMXObjectGroupInfo::TMXObjectGroupInfo()
: _groupName("")
{
}
TMXObjectGroupInfo::~TMXObjectGroupInfo()
{
    CCLOGINFO("deallocing TMXObjectGroup: %p", this);
}
// implementation TMXTilesetInfo
TMXTilesetInfo::TMXTilesetInfo()
    :_firstGid(0)
    ,_tileSize(Size::ZERO)
    ,_spacing(0)
    ,_margin(0)
    ,_preloadedTexture(nullptr)
    ,_imageSize(Size::ZERO)
{
}

TMXTilesetInfo::~TMXTilesetInfo()
{
    CCLOGINFO("deallocing TMXTilesetInfo: %p", this);
}

Rect TMXTilesetInfo::getRectForGID(uint32_t gid)
{
    Rect rect;
    rect.size = _tileSize;
    gid &= kTMXFlippedMask;
    gid = gid - _firstGid;
    // max_x means the column count in tile map
    // in the origin:
    // max_x = (int)((_imageSize.width - _margin*2 + _spacing) / (_tileSize.width + _spacing));
    // but in editor "Tiled", _margin variable only effect the left side
    // for compatible with "Tiled", change the max_x calculation
    int max_x = (int)((_imageSize.width - _margin + _spacing) / (_tileSize.width + _spacing));

    rect.origin.x = (gid % max_x) * (_tileSize.width + _spacing) + _margin;
    rect.origin.y = (gid / max_x) * (_tileSize.height + _spacing) + _margin;
    return rect;
}

// implementation TMXMapInfo

TMXMapInfo * TMXMapInfo::create(const std::string& tmxFile)
{
    TMXMapInfo *ret = new (std::nothrow) TMXMapInfo();
    if (ret->initWithTMXFile(tmxFile))
    {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

TMXMapInfo * TMXMapInfo::createWithXML(const std::string& tmxString, const std::string& resourcePath,
                                       const TMXMapInfo::TextureMap* textures)
{
    TMXMapInfo *ret = new (std::nothrow) TMXMapInfo();
    if (ret->initWithXML(tmxString, resourcePath, textures))
    {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void TMXMapInfo::internalInit(const std::string& tmxFileName, const std::string& resourcePath,
                              const TMXMapInfo::TextureMap* textures)
{
    if (!tmxFileName.empty())
    {
        _TMXFileName = FileUtils::getInstance()->fullPathForFilename(tmxFileName);
    }

    if (!resourcePath.empty())
    {
        _resources = resourcePath;
    }

    _preloadedTextures = textures;

    _objectGroups.reserve(4);

    // tmp vars
    _currentString = "";
    _storingCharacters = false;
    _layerAttribs = TMXLayerAttribNone;
    _parentElement = TMXPropertyNone;
    _currentFirstGID = -1;
}

bool TMXMapInfo::initWithXML(const std::string& tmxString, const std::string& resourcePath,
                             const TMXMapInfo::TextureMap* textures)
{
    internalInit("", resourcePath, textures);
    return parseXMLString(tmxString);
}

bool TMXMapInfo::initWithTMXFile(const std::string& tmxFile)
{
    internalInit(tmxFile, "", nullptr);
    return parseXMLFile(_TMXFileName);
}

TMXMapInfo::TMXMapInfo()
: _orientation(TMXOrientationOrtho)
, _staggerAxis(TMXStaggerAxis_Y)
, _staggerIndex(TMXStaggerIndex_Even)
, _hexSideLength(0)
, _parentElement(0)
, _parentGID(0)
, _mapSize(Size::ZERO)
, _tileSize(Size::ZERO)
, _layerAttribs(0)
, _storingCharacters(false)
, _preloadedTextures(nullptr)
, _xmlTileIndex(0)
, _currentFirstGID(-1)
, _recordFirstGID(true)
{
}

TMXMapInfo::~TMXMapInfo()
{
    CCLOGINFO("deallocing TMXMapInfo: %p", this);
}

bool TMXMapInfo::parseXMLString(const std::string& xmlString)
{
    size_t len = xmlString.size();
    if (len <= 0)
        return false;

    SAXParser parser;

    if (false == parser.init("UTF-8") )
    {
        return false;
    }

    parser.setDelegator(this);

    return parser.parse(xmlString.c_str(), len);
}

bool TMXMapInfo::parseXMLFile(const std::string& xmlFilename)
{
    SAXParser parser;

    if (false == parser.init("UTF-8") )
    {
        return false;
    }

    parser.setDelegator(this);

    return parser.parse(FileUtils::getInstance()->fullPathForFilename(xmlFilename));
}

// the XML parser calls here with all the elements
void TMXMapInfo::startElement(void *ctx, const char *name, const char **atts)
{
    CC_UNUSED_PARAM(ctx);
    TMXMapInfo *tmxMapInfo = this;
    std::string elementName = name;
    ValueMap attributeDict;
    if (atts && atts[0])
    {
        for (int i = 0; atts[i]; i += 2)
        {
            std::string key = atts[i];
            std::string value = atts[i+1];
            attributeDict.insert(std::make_pair(key, Value(value)));
        }
    }
    if (elementName == "map")
    {
        std::string version = attributeDict["version"].asString();
        if ( version != "1.0")
        {
            CCLOG("cocos2d: TMXFormat: Unsupported TMX version: %s", version.c_str());
        }
        std::string orientationStr = attributeDict["orientation"].asString();
        if (orientationStr == "orthogonal") {
            tmxMapInfo->setOrientation(TMXOrientationOrtho);
        }
        else if (orientationStr  == "isometric") {
            tmxMapInfo->setOrientation(TMXOrientationIso);
        }
        else if (orientationStr == "hexagonal") {
            tmxMapInfo->setOrientation(TMXOrientationHex);
        }
        else if (orientationStr == "staggered") {
            tmxMapInfo->setOrientation(TMXOrientationStaggered);
        }
        else {
            CCLOG("cocos2d: TMXFomat: Unsupported orientation: %d", tmxMapInfo->getOrientation());
        }
        std::string staggerAxisStr = attributeDict["staggeraxis"].asString();
        if (staggerAxisStr == "x") {
            tmxMapInfo->setStaggerAxis(TMXStaggerAxis_X);
        }
        else if (staggerAxisStr  == "y") {
            tmxMapInfo->setStaggerAxis(TMXStaggerAxis_Y);
        }

        std::string staggerIndex = attributeDict["staggerindex"].asString();
        if (staggerIndex == "odd") {
            tmxMapInfo->setStaggerIndex(TMXStaggerIndex_Odd);
        }
        else if (staggerIndex == "even") {
            tmxMapInfo->setStaggerIndex(TMXStaggerIndex_Even);
        }
        float hexSideLength = attributeDict["hexsidelength"].asFloat();
        tmxMapInfo->setHexSideLength(hexSideLength);
        Size s;
        s.width = attributeDict["width"].asFloat();
        s.height = attributeDict["height"].asFloat();
        tmxMapInfo->setMapSize(s);

        s.width = attributeDict["tilewidth"].asFloat();
        s.height = attributeDict["tileheight"].asFloat();
        tmxMapInfo->setTileSize(s);

        // The parent element is now "map"
        tmxMapInfo->setParentElement(TMXPropertyMap);
    }
    else if (elementName == "tileset")
    {
        // If this is an external tileset then start parsing that
        std::string externalTilesetFilename = attributeDict["source"].asString();
        if (externalTilesetFilename != "")
        {
            _externalTilesetFilename = externalTilesetFilename;
            // Tileset file will be relative to the map file. So we need to convert it to an absolute path
            if (_TMXFileName.find_last_of("/") != string::npos)
            {
                string dir = _TMXFileName.substr(0, _TMXFileName.find_last_of("/") + 1);
                externalTilesetFilename = dir + externalTilesetFilename;
            }
            else
            {
                externalTilesetFilename = _resources + "/" + externalTilesetFilename;
            }
            externalTilesetFilename = FileUtils::getInstance()->fullPathForFilename(externalTilesetFilename);

            _currentFirstGID = attributeDict["firstgid"].asInt();
            if (_currentFirstGID < 0)
            {
                _currentFirstGID = 0;
            }
            _recordFirstGID = false;

            tmxMapInfo->parseXMLFile(externalTilesetFilename);
        }
        else
        {
            TMXTilesetInfo *tileset = new (std::nothrow) TMXTilesetInfo();
            tileset->_name = attributeDict["name"].asString();

            if (_recordFirstGID)
            {
                // unset before, so this is tmx file.
                tileset->_firstGid = attributeDict["firstgid"].asInt();

                if (tileset->_firstGid < 0)
                {
                    tileset->_firstGid = 0;
                }
            }
            else
            {
                tileset->_firstGid = _currentFirstGID;
                _currentFirstGID = 0;
            }

            tileset->_spacing = attributeDict["spacing"].asInt();
            tileset->_margin = attributeDict["margin"].asInt();
            Size s;
            s.width = attributeDict["tilewidth"].asFloat();
            s.height = attributeDict["tileheight"].asFloat();
            tileset->_tileSize = s;

            tmxMapInfo->getTilesets().pushBack(tileset);
            tileset->release();
        }
    }
    else if (elementName == "tile")
    {
        if (tmxMapInfo->getParentElement() == TMXPropertyLayer)
        {
            TMXLayerInfo* layer = tmxMapInfo->getLayers().back();
            Size layerSize = layer->_layerSize;
            uint32_t gid = static_cast<uint32_t>(attributeDict["gid"].asInt());
            int tilesAmount = layerSize.width*layerSize.height;

            if (_xmlTileIndex < tilesAmount)
            {
                layer->_tiles[_xmlTileIndex++] = gid;
            }
        }
        else
        {
            TMXTilesetInfo* info = tmxMapInfo->getTilesets().back();
            tmxMapInfo->setParentGID(info->_firstGid + attributeDict["id"].asInt());
            tmxMapInfo->getTileProperties()[tmxMapInfo->getParentGID()] = Value(ValueMap());
            tmxMapInfo->setParentElement(TMXPropertyTile);
        }
    }
    else if (elementName == "layer")
    {
        TMXLayerInfo *layer = new (std::nothrow) TMXLayerInfo();
        layer->_name = attributeDict["name"].asString();

        Size s;
        s.width = attributeDict["width"].asFloat();
        s.height = attributeDict["height"].asFloat();
        layer->_layerSize = s;

        Value& visibleValue = attributeDict["visible"];
        layer->_visible = visibleValue.isNull() ? true : visibleValue.asBool();

        Value& opacityValue = attributeDict["opacity"];
        layer->_opacity = opacityValue.isNull() ? 255 : (unsigned char)(255.0f * opacityValue.asFloat());

        float x = attributeDict["x"].asFloat();
        float y = attributeDict["y"].asFloat();
        layer->_offset.set(x, y);

        tmxMapInfo->getAllChildren().pushBack(layer);
        tmxMapInfo->getLayers().pushBack(layer);
        layer->release();

        // The parent element is now "layer"
        tmxMapInfo->setParentElement(TMXPropertyLayer);
    }
    else if (elementName == "objectgroup")
    {
        TMXObjectGroupInfo *objectGroup = new (std::nothrow) TMXObjectGroupInfo();
        objectGroup->_groupName = attributeDict["name"].asString();
        Vec2 positionOffset;
        positionOffset.x = attributeDict["offsetx"].asFloat();
        positionOffset.y = attributeDict["offsety"].asFloat();
        objectGroup->_positionOffset = CC_POINT_PIXELS_TO_POINTS(positionOffset);
        
        // object group color
        Value& colorValue = attributeDict["color"];
        if (colorValue.isNull()) {
            objectGroup->_color = Color3B(255, 255, 255);
        } else {
            std::string colorStr = colorValue.asString();
            auto startPos = colorStr.find("#");
            if (startPos != string::npos) {
                colorStr.replace(startPos, 1, "");
            }
            // Android NDK 10 doesn't support std::stoi a/ std::stoul
#if CC_TARGET_PLATFORM != CC_PLATFORM_ANDROID
            int num = std::stoi(colorStr, nullptr, 16);
#else
            int num = atoi(colorStr.c_str());
#endif
            int r = num / 0x10000;
            int g = (num / 0x100) % 0x100;
            int b = num % 0x100;
            
            objectGroup->_color = Color3B(r, g, b);
        }
        
        // object group opacity
        Value& opacityValue = attributeDict["opacity"];
        float percent = opacityValue.isNull() ? 1.0 : opacityValue.asFloat();
        objectGroup->_opacity = 255 * percent;
        
        // object group visible
        Value& visibleValue = attributeDict["visible"];
        objectGroup->_visible = visibleValue.isNull() ? true : visibleValue.asBool();

        tmxMapInfo->getAllChildren().pushBack(objectGroup);
        tmxMapInfo->getObjectGroups().pushBack(objectGroup);
        objectGroup->release();

        // The parent element is now "objectgroup"
        tmxMapInfo->setParentElement(TMXPropertyObjectGroup);
    }
    else if (elementName == "tileoffset")
    {
        TMXTilesetInfo* tileset = tmxMapInfo->getTilesets().back();
        double tileOffsetX = attributeDict["x"].asDouble();
        double tileOffsetY = attributeDict["y"].asDouble();
        tileset->_tileOffset = Vec2(tileOffsetX, tileOffsetY);
    }
    else if (elementName == "image")
    {
        TMXTilesetInfo* tileset = tmxMapInfo->getTilesets().back();

        // build full path
        std::string imagename = attributeDict["source"].asString();
        tileset->_originSourceImage = imagename;

        if (_preloadedTextures != nullptr)
        {
            auto it = _preloadedTextures->find(imagename);
            if (it != _preloadedTextures->end())
            {
                tileset->_preloadedTexture = it->second;
            }
            else
            {
                CCLOG("cocos2d: TiledMap: Texture '%s' not found.", imagename.c_str());
            }
        }
        else
        {
            if (_TMXFileName.find_last_of("/") != string::npos)
            {
                string dir = _TMXFileName.substr(0, _TMXFileName.find_last_of("/") + 1);
                tileset->_sourceImage = dir + imagename;
            }
            else
            {
                tileset->_sourceImage = _resources + (_resources.size() ? "/" : "") + imagename;
            }
        }
    }
    else if (elementName == "data")
    {
        std::string encoding = attributeDict["encoding"].asString();
        std::string compression = attributeDict["compression"].asString();

        if (encoding == "")
        {
            tmxMapInfo->setLayerAttribs(tmxMapInfo->getLayerAttribs() | TMXLayerAttribNone);

            TMXLayerInfo* layer = tmxMapInfo->getLayers().back();
            Size layerSize = layer->_layerSize;
            int tilesAmount = layerSize.width*layerSize.height;

            uint32_t *tiles = (uint32_t*) malloc(tilesAmount*sizeof(uint32_t));
            // set all value to 0
            memset(tiles, 0, tilesAmount*sizeof(int));

            layer->_tiles = tiles;
        }
        else if (encoding == "base64")
        {
            int layerAttribs = tmxMapInfo->getLayerAttribs();
            tmxMapInfo->setLayerAttribs(layerAttribs | TMXLayerAttribBase64);
            tmxMapInfo->setStoringCharacters(true);

            if (compression == "gzip")
            {
                layerAttribs = tmxMapInfo->getLayerAttribs();
                tmxMapInfo->setLayerAttribs(layerAttribs | TMXLayerAttribGzip);
            } else
            if (compression == "zlib")
            {
                layerAttribs = tmxMapInfo->getLayerAttribs();
                tmxMapInfo->setLayerAttribs(layerAttribs | TMXLayerAttribZlib);
            }
            CCASSERT( compression == "" || compression == "gzip" || compression == "zlib", "TMX: unsupported compression method" );
        }
        else if (encoding == "csv")
        {
            int layerAttribs = tmxMapInfo->getLayerAttribs();
            tmxMapInfo->setLayerAttribs(layerAttribs | TMXLayerAttribCSV);
            tmxMapInfo->setStoringCharacters(true);
        }
    }
    else if (elementName == "object")
    {
        TMXObjectGroupInfo* objectGroup = tmxMapInfo->getObjectGroups().back();

        // The value for "type" was blank or not a valid class name
        // Create an instance of TMXObjectInfo to store the object and its properties
        ValueMap dict;
        // Parse everything automatically
        const char* keys[] = {"name", "type", "width", "height", "gid", "id"};

        for (const auto& key : keys)
        {
            Value value = attributeDict[key];
            dict[key] = value;
        }

        // But X and Y since they need special treatment
        // X
        int x = attributeDict["x"].asFloat();
        // Y
        int y = attributeDict["y"].asFloat();

        dict["x"] = Value(x);
        dict["y"] = Value(y);
        
        int width = attributeDict["width"].asFloat();
        int height = attributeDict["height"].asFloat();
        dict["width"] = Value(width);
        dict["height"] = Value(height);
        
        // visible
        Value& visibleValue = attributeDict["visible"];
        dict["visible"] = Value(visibleValue.isNull() ? true : visibleValue.asBool());
        
        // rotation
        Value& rotationValue = attributeDict["rotation"];
        dict["rotation"] = Value(rotationValue.isNull() ? 0.0 : rotationValue.asFloat());

        // default type is rect
        dict["type"] = Value(static_cast<int>(TMXObjectType::RECT));

        // if has gid, the type is image
        if (!dict["gid"].isNull()) {
            dict["type"] = Value(static_cast<int>(TMXObjectType::IMAGE));
        }

        // Add the object to the objectGroup
        objectGroup->_objects.push_back(Value(dict));

        // The parent element is now "object"
        tmxMapInfo->setParentElement(TMXPropertyObject);
    }
    else if (elementName == "property")
    {
        if ( tmxMapInfo->getParentElement() == TMXPropertyNone )
        {
            CCLOG( "TMX tile map: Parent element is unsupported. Cannot add property named '%s' with value '%s'",
                  attributeDict["name"].asString().c_str(), attributeDict["value"].asString().c_str() );
        }
        else if ( tmxMapInfo->getParentElement() == TMXPropertyMap )
        {
            // The parent element is the map
            Value value = attributeDict["value"];
            std::string key = attributeDict["name"].asString();
            tmxMapInfo->getProperties().insert(std::make_pair(key, value));
        }
        else if ( tmxMapInfo->getParentElement() == TMXPropertyLayer )
        {
            // The parent element is the last layer
            TMXLayerInfo* layer = tmxMapInfo->getLayers().back();
            Value value = attributeDict["value"];
            std::string key = attributeDict["name"].asString();
            // Add the property to the layer
            layer->getProperties().insert(std::make_pair(key, value));
        }
        else if ( tmxMapInfo->getParentElement() == TMXPropertyObjectGroup )
        {
            // The parent element is the last object group
            TMXObjectGroupInfo* objectGroup = tmxMapInfo->getObjectGroups().back();
            Value value = attributeDict["value"];
            std::string key = attributeDict["name"].asString();
            objectGroup->getProperties().insert(std::make_pair(key, value));
        }
        else if ( tmxMapInfo->getParentElement() == TMXPropertyObject )
        {
            // The parent element is the last object
            TMXObjectGroupInfo* objectGroup = tmxMapInfo->getObjectGroups().back();
            ValueMap& dict = objectGroup->_objects.rbegin()->asValueMap();

            std::string propertyName = attributeDict["name"].asString();
            dict[propertyName] = attributeDict["value"];
        }
        else if ( tmxMapInfo->getParentElement() == TMXPropertyTile )
        {
            ValueMap& dict = tmxMapInfo->getTileProperties().at(tmxMapInfo->getParentGID()).asValueMap();

            std::string propertyName = attributeDict["name"].asString();
            dict[propertyName] = attributeDict["value"];
        }
    }
    else if (elementName == "polygon")
    {
        // find parent object's dict and add polygon-points to it
        TMXObjectGroupInfo* objectGroup = _objectGroups.back();
        ValueMap& dict = objectGroup->_objects.rbegin()->asValueMap();
        dict["type"] = Value(static_cast<int>(TMXObjectType::POLYGON));

        // get points value string
        std::string value = attributeDict["points"].asString();
        if (!value.empty())
        {
            ValueVector pointsArray;
            pointsArray.reserve(10);

            // parse points string into a space-separated set of points
            stringstream pointsStream(value);
            string pointPair;
            while (std::getline(pointsStream, pointPair, ' '))
            {
                // parse each point combo into a comma-separated x,y point
                stringstream pointStream(pointPair);
                string xStr, yStr;

                ValueMap pointDict;

                // set x
                if (std::getline(pointStream, xStr, ','))
                {
                    pointDict["x"] = Value(atof(xStr.c_str()));
                }

                // set y
                if (std::getline(pointStream, yStr, ','))
                {
                    pointDict["y"] = Value(atof(yStr.c_str()));
                }

                // add to points array
                pointsArray.push_back(Value(pointDict));
            }

            dict["points"] = Value(pointsArray);
        }
    }
    else if (elementName == "polyline")
    {
        // find parent object's dict and add polyline-points to it
        TMXObjectGroupInfo* objectGroup = _objectGroups.back();
        ValueMap& dict = objectGroup->_objects.rbegin()->asValueMap();
        dict["type"] = Value(static_cast<int>(TMXObjectType::POLYLINE));
        // get points value string
        std::string value = attributeDict["points"].asString();
        if (!value.empty())
        {
            ValueVector pointsArray;
            pointsArray.reserve(10);

            // parse points string into a space-separated set of points
            stringstream pointsStream(value);
            string pointPair;
            while (std::getline(pointsStream, pointPair, ' '))
            {
                // parse each point combo into a comma-separated x,y point
                stringstream pointStream(pointPair);
                string xStr, yStr;

                ValueMap pointDict;

                // set x
                if (std::getline(pointStream, xStr, ','))
                {
                    pointDict["x"] = Value(atof(xStr.c_str()));
                }

                // set y
                if (std::getline(pointStream, yStr, ','))
                {
                    pointDict["y"] = Value(atof(yStr.c_str()));
                }

                // add to points array
                pointsArray.push_back(Value(pointDict));
            }

            dict["polylinePoints"] = Value(pointsArray);
        }
    }
    else if (elementName == "ellipse") {
        TMXObjectGroupInfo* objectGroup = _objectGroups.back();
        ValueMap& dict = objectGroup->_objects.rbegin()->asValueMap();
        dict["type"] = Value(static_cast<int>(TMXObjectType::ELLIPSE));
    }
}

void TMXMapInfo::endElement(void *ctx, const char *name)
{
    CC_UNUSED_PARAM(ctx);
    TMXMapInfo *tmxMapInfo = this;
    std::string elementName = name;

    if (elementName == "data")
    {
        if (tmxMapInfo->getLayerAttribs() & TMXLayerAttribBase64)
        {
            tmxMapInfo->setStoringCharacters(false);

            TMXLayerInfo* layer = tmxMapInfo->getLayers().back();

            std::string currentString = tmxMapInfo->getCurrentString();
            unsigned char *buffer;
            auto len = base64Decode((unsigned char*)currentString.c_str(), (unsigned int)currentString.length(), &buffer);
            if (!buffer)
            {
                CCLOG("cocos2d: TiledMap: decode data error");
                return;
            }

            if (tmxMapInfo->getLayerAttribs() & (TMXLayerAttribGzip | TMXLayerAttribZlib))
            {
                unsigned char *deflated = nullptr;
                Size s = layer->_layerSize;
                // int sizeHint = s.width * s.height * sizeof(uint32_t);
                ssize_t sizeHint = s.width * s.height * sizeof(unsigned int);

                ssize_t CC_UNUSED inflatedLen = ZipUtils::inflateMemoryWithHint(buffer, len, &deflated, sizeHint);
                CCASSERT(inflatedLen == sizeHint, "inflatedLen should be equal to sizeHint!");

                free(buffer);
                buffer = nullptr;

                if (!deflated)
                {
                    CCLOG("cocos2d: TiledMap: inflate data error");
                    return;
                }

                layer->_tiles = reinterpret_cast<uint32_t*>(deflated);
            }
            else
            {
                layer->_tiles = reinterpret_cast<uint32_t*>(buffer);
            }

            tmxMapInfo->setCurrentString("");
        }
        else if (tmxMapInfo->getLayerAttribs() & TMXLayerAttribCSV)
        {
            unsigned char *buffer;
            
            TMXLayerInfo* layer = tmxMapInfo->getLayers().back();
            
            tmxMapInfo->setStoringCharacters(false);
            std::string currentString = tmxMapInfo->getCurrentString();
            
            vector<string> gidTokens;
            istringstream filestr(currentString);
            string sRow;
            while(getline(filestr, sRow, '\n')) {
                string sGID;
                istringstream rowstr(sRow);
                while (getline(rowstr, sGID, ',')) {
                    gidTokens.push_back(sGID);
                }
            }
            
            // 32-bits per gid
            buffer = (unsigned char*)malloc(gidTokens.size() * 4);
            if (!buffer)
            {
                CCLOG("cocos2d: TiledMap: CSV buffer not allocated.");
                return;
            }
            
            uint32_t* bufferPtr = reinterpret_cast<uint32_t*>(buffer);
            for(auto gidToken : gidTokens) {
                auto tileGid = (uint32_t)strtol(gidToken.c_str(), nullptr, 10);
                *bufferPtr = tileGid;
                bufferPtr++;
            }
            
            layer->_tiles = reinterpret_cast<uint32_t*>(buffer);
            
            tmxMapInfo->setCurrentString("");
        }
        else if (tmxMapInfo->getLayerAttribs() & TMXLayerAttribNone)
        {
            _xmlTileIndex = 0;
        }
    }
    else if (elementName == "map")
    {
        // The map element has ended
        tmxMapInfo->setParentElement(TMXPropertyNone);
    }
    else if (elementName == "layer")
    {
        // The layer element has ended
        tmxMapInfo->setParentElement(TMXPropertyNone);
    }
    else if (elementName == "objectgroup")
    {
        // The objectgroup element has ended
        tmxMapInfo->setParentElement(TMXPropertyNone);
    }
    else if (elementName == "object")
    {
        // The object element has ended
        tmxMapInfo->setParentElement(TMXPropertyNone);
    }
    else if (elementName == "tileset")
    {
        _recordFirstGID = true;
    }
}

void TMXMapInfo::textHandler(void *ctx, const char *ch, int len)
{
    CC_UNUSED_PARAM(ctx);
    TMXMapInfo *tmxMapInfo = this;
    std::string text(ch, 0, len);

    if (tmxMapInfo->isStoringCharacters())
    {
        std::string currentString = tmxMapInfo->getCurrentString();
        currentString += text;
        tmxMapInfo->setCurrentString(currentString);
    }
}

NS_CC_END

