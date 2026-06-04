// *****************************************************************************
// *****************************************************************************
// oaAiviText.cpp
//
// Native OpenAccess text bounding-box plug-in.
//
// This implementation follows the expected native OpenAccess text plug-in contract:
//   - class oaNativeText : PlugInBase<IText>
//   - getName() returns "oaNativeText"
//   - oaNativeTextInit() registers both "oaNativeText" and "oaTextSystem"
//   - getClassObject() delegates to FactoryBase::getClassObject()
//   - getBBox() uses oaFont::calcBBox(...)
//
// *****************************************************************************

#include <oa/oaCommonPlugInBase.h>
#include <oa/oaCommonPlugInBase.inl>
#include <oa/oaCommonFactory.h>
#include <oa/oaCommonFactory.inl>
#include <oa/oaCommonPlugInMgr.h>
#include <oa/oaDesignInterfaces.h>
#include <oa/oaDesignInterfaces.inl>
#include <oa/oaText.h>
#include <oa/oaTextDisplay.h>
#include <oa/oaOrient.h>
#include <oa/oaOrient.inl>
#include <oa/oaFont.h>
#include <oa/oaBox.h>
#include <oa/oaBox.inl>
#include <oa/oaPoint.h>
#include <oa/oaPoint.inl>
#include <oa/oaString.h>
#include <oa/oaString.inl>
#include <oa/oaDesignEnumWrapper.inl>

BEGIN_OA_NAMESPACE

class oaNativeText : public oaCommon::PlugInBase<IText> {
public:
                            oaNativeText()
                            : oaCommon::PlugInBase<IText>() {}

    virtual void            init(ITextInvalidate *) {}

    virtual void            getName(oaString &name)
                            { name = componentName; }

    virtual void            getBBox(const oaText *text,
                                    oaBox        &bBox)
    {
        oaString        textString;
        oaPoint         origin;
        oaFont          font(text->getFont());
        oaBoolean       hasOverbar = text->hasOverbar();
        oaOrient        orient = text->getOrient();
        oaTextAlign     align = text->getAlignment();
        oaDist          height = text->getHeight();

        text->getText(textString);
        text->getOrigin(origin);
        font.calcBBox(origin, textString, height, align, orient, hasOverbar, bBox);
    }

    virtual void            getBBox(const oaTextDisplay *textDisplay,
                                    oaBox               &bBox)
    {
        oaString        textString;
        oaPoint         origin;
        oaFont          font(textDisplay->getFont());
        oaBoolean       hasOverbar = textDisplay->hasOverbar();
        oaOrient        orient = textDisplay->getOrient();
        oaTextAlign     align = textDisplay->getAlignment();
        oaDist          height = textDisplay->getHeight();

        textDisplay->getText(textString);
        textDisplay->getOrigin(origin);
        font.calcBBox(origin, textString, height, align, orient, hasOverbar, bBox);
    }

    static oaString                         componentName;
    static oaCommon::Factory<oaNativeText> factory;
};

oaString oaNativeText::componentName("oaNativeText");
oaCommon::Factory<oaNativeText> oaNativeText::factory((const oaChar*)oaNativeText::componentName);

END_OA_NAMESPACE

extern "C" void oaNativeTextInit()
{
    const char *componentName = OpenAccess_4::oaNativeText::componentName;
    oaCommon::oaPlugInMgr::registerFactory(componentName,
                                           &OpenAccess_4::oaNativeText::factory);
    oaCommon::oaPlugInMgr::registerFactory("oaTextSystem",
                                           &OpenAccess_4::oaNativeText::factory);
}

extern "C" long getClassObject(const char *cid,
                                const oaCommon::Guid &iid,
                                void **inst)
{
    return oaCommon::FactoryBase::getClassObject(cid, iid, inst);
}
