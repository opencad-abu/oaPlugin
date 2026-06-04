// oaAiviPcell bridge - acell Python3 PCell evaluator for OpenAccess.
//
// This file is a reviewable replacement candidate for the companion oaAiviPcell implementation.
//
// PcellDef data keys:
//   AcellModule / AcellFunc  - preferred acell evaluator module/function
//   PyModule / PyFunc        - compatibility aliases used by OA lab 18-8
//   AcellVersion / AcellSchema are persisted for future migration.

#include <oa/oaCommonFactory.h>
#include <oa/oaCommonPlugInBase.h>
#include <oa/oaBooleanProp.h>
#include <oa/oaDesignDB.h>
#include <oa/oaDoubleProp.h>
#include <oa/oaFloatProp.h>
#include <oa/oaIntProp.h>
#include <oa/oaPlugIn.h>
#include <oa/oaStringProp.h>

#include <Python.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace oa;
using namespace oaPlugIn;

namespace oaAiviPcell {
namespace {

const char *kClassId = "oaAiviPcell";
const char *kLegacyClassId = "oaPcellAIVI";
const char *kAcellModule = "AcellModule";
const char *kAcellFunc = "AcellFunc";
const char *kAcellVersion = "AcellVersion";
const char *kAcellSchema = "AcellSchema";
const char *kPyModule = "PyModule";
const char *kPyFunc = "PyFunc";
const char *kDefaultFunc = "eval";
const oaUInt4 kDiskSchema = 1;

struct MetadataPair {
    const char *key;
    oaString value;
};

struct FigureRef {
    FigureRef() : fig(NULL), pinFig(false) {}
    FigureRef(oaFig *figIn, bool pinFigIn) : fig(figIn), pinFig(pinFigIn) {}

    oaFig *fig;
    bool pinFig;
};

typedef std::map<std::string, FigureRef> FigureMap;

class PyRef {
public:
    explicit PyRef(PyObject *obj = NULL) : obj_(obj) {}
    ~PyRef() { Py_XDECREF(obj_); }

    PyRef(const PyRef &) = delete;
    PyRef &operator=(const PyRef &) = delete;

    PyObject *get() const { return obj_; }
    PyObject *release()
    {
        PyObject *obj = obj_;
        obj_ = NULL;
        return obj;
    }

private:
    PyObject *obj_;
};

void logInfo(const std::string &msg)
{
    std::cerr << "[oaAiviPcell] " << msg << std::endl;
}

void logPyError(const std::string &msg)
{
    std::cerr << "[oaAiviPcell] ERROR: " << msg << std::endl;
    if (PyErr_Occurred()) {
        PyErr_Print();
    }
}

std::string lower(std::string value)
{
    for (std::string::size_type i = 0; i < value.size(); ++i) {
        if (value[i] >= 'A' && value[i] <= 'Z') {
            value[i] = static_cast<char>(value[i] - 'A' + 'a');
        }
    }
    return value;
}

std::string oaToString(const oaString &value)
{
    return std::string(static_cast<const char *>(value));
}

oaUInt4 stringDiskSize(const oaString &value)
{
    return (value.getLength() + 8) & ~static_cast<oaUInt4>(7);
}

bool getData(oaPcellDef *def, const char *key, oaString &value)
{
    value = "";
    return def && def->getDataValue(oaString(key), value) && value.getLength() != 0;
}

void setData(oaPcellDef *def, const char *key, const oaString &value)
{
    if (!def) {
        return;
    }

    oaString oldValue;
    if (def->getDataValue(oaString(key), oldValue)) {
        def->setDataValue(oaString(key), value);
    } else {
        def->addData(oaString(key), value);
    }
}

oaString getModule(oaPcellDef *def)
{
    oaString value;
    if (getData(def, kAcellModule, value)) {
        return value;
    }
    getData(def, kPyModule, value);
    return value;
}

oaString getFunc(oaPcellDef *def)
{
    oaString value;
    if (!getData(def, kAcellFunc, value)) {
        getData(def, kPyFunc, value);
    }
    if (value.getLength() == 0) {
        value = kDefaultFunc;
    }
    return value;
}

void normalizeMetadata(oaPcellDef *def)
{
    oaString module = getModule(def);
    oaString func = getFunc(def);

    setData(def, kAcellModule, module);
    setData(def, kAcellFunc, func);
    setData(def, kPyModule, module);
    setData(def, kPyFunc, func);

    oaString value;
    if (!getData(def, kAcellVersion, value)) {
        setData(def, kAcellVersion, "0");
    }
    if (!getData(def, kAcellSchema, value)) {
        setData(def, kAcellSchema, "1");
    }
}

std::vector<MetadataPair> collectMetadata(oaPcellDef *def, bool writeBack)
{
    oaString module = getModule(def);
    oaString func = getFunc(def);
    oaString version;
    oaString schema;

    if (!getData(def, kAcellVersion, version)) {
        version = "0";
    }
    if (!getData(def, kAcellSchema, schema)) {
        schema = "1";
    }

    if (writeBack) {
        setData(def, kAcellModule, module);
        setData(def, kAcellFunc, func);
        setData(def, kAcellVersion, version);
        setData(def, kAcellSchema, schema);
        setData(def, kPyModule, module);
        setData(def, kPyFunc, func);
    }

    std::vector<MetadataPair> pairs;
    pairs.push_back(MetadataPair{kAcellModule, module});
    pairs.push_back(MetadataPair{kAcellFunc, func});
    pairs.push_back(MetadataPair{kAcellVersion, version});
    pairs.push_back(MetadataPair{kAcellSchema, schema});
    pairs.push_back(MetadataPair{kPyModule, module});
    pairs.push_back(MetadataPair{kPyFunc, func});
    return pairs;
}

void applyAcellPythonPath()
{
    const char *extraPath = std::getenv("ACELL_PYTHONPATH");
    if (!extraPath || !*extraPath) {
        return;
    }

    PyObject *sysPath = PySys_GetObject(const_cast<char *>("path")); // borrowed
    if (!sysPath || !PyList_Check(sysPath)) {
        return;
    }

    std::string all(extraPath);
    std::string::size_type begin = 0;
    while (begin <= all.size()) {
        std::string::size_type end = all.find(':', begin);
        std::string item = all.substr(begin, end == std::string::npos ? end : end - begin);
        if (!item.empty()) {
            PyRef pyItem(PyUnicode_FromString(item.c_str()));
            if (pyItem.get()) {
                PyList_Insert(sysPath, 0, pyItem.get());
            } else {
                PyErr_Clear();
            }
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
}

void ensurePython()
{
    static bool pathApplied = false;

    if (!Py_IsInitialized()) {
        Py_Initialize();
        logInfo(std::string("initialized Python ") + Py_GetVersion());
    }

    if (!pathApplied) {
        PyGILState_STATE gil = PyGILState_Ensure();
        applyAcellPythonPath();
        PyGILState_Release(gil);
        pathApplied = true;
    }
}

PyObject *paramToPyObject(const oaParam &param)
{
    switch (static_cast<oaParamTypeEnum>(param.getType())) {
    case oacIntParamType:
        return PyLong_FromLong(static_cast<long>(param.getIntVal()));
    case oacFloatParamType:
        return PyFloat_FromDouble(static_cast<double>(param.getFloatVal()));
    case oacDoubleParamType:
        return PyFloat_FromDouble(static_cast<double>(param.getDoubleVal()));
    case oacBooleanParamType:
        return PyBool_FromLong(param.getBooleanVal() ? 1 : 0);
    case oacStringParamType: {
        oaString value;
        param.getStringVal(value);
        return PyUnicode_FromString(static_cast<const char *>(value));
    }
    case oacTimeParamType:
        return PyLong_FromLongLong(static_cast<long long>(param.getTimeVal()));
    case oacAppParamType:
    default:
        Py_INCREF(Py_None);
        return Py_None;
    }
}

PyObject *paramsToPyDict(oaDesign *design)
{
    PyObject *dict = PyDict_New();
    if (!dict) {
        return NULL;
    }

    oaParamArray params;
    design->getParams(params);
    for (oaUInt4 i = 0; i < params.getNumElements(); ++i) {
        const oaParam &param = params[i];
        oaString name;
        param.getName(name);

        PyRef value(paramToPyObject(param));
        if (!value.get()) {
            Py_DECREF(dict);
            return NULL;
        }

        if (PyDict_SetItemString(dict, static_cast<const char *>(name), value.get()) != 0) {
            Py_DECREF(dict);
            return NULL;
        }
    }

    return dict;
}

bool pyDictSetString(PyObject *dict, const char *key, const std::string &value)
{
    PyRef pyValue(PyUnicode_FromString(value.c_str()));
    return pyValue.get() && PyDict_SetItemString(dict, key, pyValue.get()) == 0;
}

bool pyDictSetLong(PyObject *dict, const char *key, long value)
{
    PyRef pyValue(PyLong_FromLong(value));
    return pyValue.get() && PyDict_SetItemString(dict, key, pyValue.get()) == 0;
}

bool pyDictSetBool(PyObject *dict, const char *key, bool value)
{
    PyObject *pyValue = value ? Py_True : Py_False;
    Py_INCREF(pyValue);
    PyRef holder(pyValue);
    return PyDict_SetItemString(dict, key, holder.get()) == 0;
}

PyObject *runtimeToPyDict(oaDesign *design)
{
    PyObject *dict = PyDict_New();
    if (!dict) {
        return NULL;
    }

    if (!pyDictSetString(dict, "bridge", "oaAiviPcell") ||
        !pyDictSetLong(dict, "schema", 1)) {
        Py_DECREF(dict);
        return NULL;
    }

    if (design) {
        oaNativeNS ns;
        oaString value;
        design->getLibName(ns, value);
        if (!pyDictSetString(dict, "lib", static_cast<const char *>(value))) {
            Py_DECREF(dict);
            return NULL;
        }
        design->getCellName(ns, value);
        if (!pyDictSetString(dict, "cell", static_cast<const char *>(value))) {
            Py_DECREF(dict);
            return NULL;
        }
        design->getViewName(ns, value);
        if (!pyDictSetString(dict, "view", static_cast<const char *>(value))) {
            Py_DECREF(dict);
            return NULL;
        }

        oaBlock *block = design->getTopBlock();
        if (block && !pyDictSetLong(dict, "dbu_per_uu", static_cast<long>(block->getDBUPerUU()))) {
            Py_DECREF(dict);
            return NULL;
        }
        if (!pyDictSetBool(dict, "has_tech", design->getTech() != NULL)) {
            Py_DECREF(dict);
            return NULL;
        }
    }

    return dict;
}

PyObject *callEvaluator(PyObject *func, PyObject *params, PyObject *runtime)
{
    PyRef args2(PyTuple_Pack(2, params, runtime ? runtime : Py_None));
    if (args2.get()) {
        PyObject *result = PyObject_CallObject(func, args2.get());
        if (result || !PyErr_ExceptionMatches(PyExc_TypeError)) {
            return result;
        }
        PyErr_Clear();
    } else {
        PyErr_Clear();
    }

    PyRef args1(PyTuple_Pack(1, params));
    if (!args1.get()) {
        return NULL;
    }
    return PyObject_CallObject(func, args1.get());
}

std::string pyToString(PyObject *obj, const char *fallback = "")
{
    if (!obj || obj == Py_None) {
        return fallback ? fallback : "";
    }

    if (PyUnicode_Check(obj)) {
        const char *text = PyUnicode_AsUTF8(obj);
        if (text) {
            return text;
        }
        PyErr_Clear();
    }

    PyRef rendered(PyObject_Str(obj));
    if (!rendered.get()) {
        PyErr_Clear();
        return fallback ? fallback : "";
    }

    const char *text = PyUnicode_AsUTF8(rendered.get());
    if (!text) {
        PyErr_Clear();
        return fallback ? fallback : "";
    }
    return text;
}

std::string jsonEscape(const std::string &value)
{
    std::string out;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        switch (*it) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += *it;
            break;
        }
    }
    return out;
}

std::string propSafeSuffix(const std::string &value)
{
    std::string out;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const char ch = *it;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.') {
            out += ch;
        } else {
            out += '_';
        }
    }
    return out.empty() ? "group" : out;
}

long pyToLong(PyObject *obj, long fallback = 0)
{
    if (!obj) {
        return fallback;
    }

    long value = PyLong_AsLong(obj);
    if (PyErr_Occurred()) {
        PyErr_Clear();
        return fallback;
    }
    return value;
}

bool pyToBool(PyObject *obj, bool fallback = false)
{
    if (!obj) {
        return fallback;
    }
    int value = PyObject_IsTrue(obj);
    if (value < 0) {
        PyErr_Clear();
        return fallback;
    }
    return value != 0;
}

PyObject *dictGet(PyObject *dict, const char *key)
{
    return dict && PyDict_Check(dict) ? PyDict_GetItemString(dict, key) : NULL;
}

PyObject *dictGetAlias(PyObject *dict, const char *a, const char *b = NULL,
                       const char *c = NULL)
{
    PyObject *value = dictGet(dict, a);
    if (!value && b) {
        value = dictGet(dict, b);
    }
    if (!value && c) {
        value = dictGet(dict, c);
    }
    return value;
}

double pyToDouble(PyObject *obj, double fallback = 0.0)
{
    if (!obj) {
        return fallback;
    }

    double value = PyFloat_AsDouble(obj);
    if (PyErr_Occurred()) {
        PyErr_Clear();
        return fallback;
    }
    return value;
}

bool pyToBox(PyObject *obj, oaBox &box)
{
    PyRef seq(PySequence_Fast(obj, "box must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return false;
    }
    if (PySequence_Fast_GET_SIZE(seq.get()) != 4) {
        return false;
    }

    long left = pyToLong(PySequence_Fast_GET_ITEM(seq.get(), 0));
    long bottom = pyToLong(PySequence_Fast_GET_ITEM(seq.get(), 1));
    long right = pyToLong(PySequence_Fast_GET_ITEM(seq.get(), 2));
    long top = pyToLong(PySequence_Fast_GET_ITEM(seq.get(), 3));
    box.set(static_cast<oaCoord>(left), static_cast<oaCoord>(bottom),
            static_cast<oaCoord>(right), static_cast<oaCoord>(top));
    return right >= left && top >= bottom;
}

bool pyToPointArray(PyObject *obj, oaPointArray &points)
{
    PyRef seq(PySequence_Fast(obj, "points must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return false;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        PyRef pair(PySequence_Fast(item, "point must be a sequence"));
        if (!pair.get()) {
            PyErr_Clear();
            return false;
        }
        if (PySequence_Fast_GET_SIZE(pair.get()) != 2) {
            return false;
        }
        long x = pyToLong(PySequence_Fast_GET_ITEM(pair.get(), 0));
        long y = pyToLong(PySequence_Fast_GET_ITEM(pair.get(), 1));
        points.append(oaPoint(static_cast<oaCoord>(x), static_cast<oaCoord>(y)));
    }
    return true;
}

bool pyToUnsignedNumber(PyObject *obj, oaUInt4 &value)
{
    if (!obj) {
        return false;
    }
    if (PyLong_Check(obj)) {
        long raw = pyToLong(obj, -1);
        if (raw < 0) {
            return false;
        }
        value = static_cast<oaUInt4>(raw);
        return true;
    }
    if (PyUnicode_Check(obj)) {
        const char *text = PyUnicode_AsUTF8(obj);
        if (!text || !*text) {
            PyErr_Clear();
            return false;
        }
        char *end = NULL;
        unsigned long raw = std::strtoul(text, &end, 10);
        if (end && *end == '\0') {
            value = static_cast<oaUInt4>(raw);
            return true;
        }
    }
    return false;
}

struct MasterRef {
    std::string lib;
    std::string cell;
    std::string view;
};

bool parseMasterPath(const std::string &text, MasterRef &ref)
{
    std::string::size_type first = text.find('/');
    std::string::size_type second = first == std::string::npos
        ? std::string::npos
        : text.find('/', first + 1);

    if (first == std::string::npos || second == std::string::npos ||
        text.find('/', second + 1) != std::string::npos) {
        first = text.find(':');
        second = first == std::string::npos
            ? std::string::npos
            : text.find(':', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
            text.find(':', second + 1) != std::string::npos) {
            return false;
        }
    }

    ref.lib = text.substr(0, first);
    ref.cell = text.substr(first + 1, second - first - 1);
    ref.view = text.substr(second + 1);
    return !ref.lib.empty() && !ref.cell.empty() && !ref.view.empty();
}

bool parseMasterRef(PyObject *item, MasterRef &ref)
{
    if (!PyDict_Check(item)) {
        return false;
    }

    PyObject *master = dictGet(item, "master");
    if (master && PyDict_Check(master)) {
        ref.lib = pyToString(dictGet(master, "lib"));
        ref.cell = pyToString(dictGet(master, "cell"));
        ref.view = pyToString(dictGet(master, "view"));
    } else if (master && master != Py_None) {
        if (!parseMasterPath(pyToString(master), ref)) {
            return false;
        }
    }

    std::string lib = pyToString(dictGet(item, "lib"));
    std::string cell = pyToString(dictGet(item, "cell"));
    std::string view = pyToString(dictGet(item, "view"));
    if (!lib.empty()) {
        ref.lib = lib;
    }
    if (!cell.empty()) {
        ref.cell = cell;
    }
    if (!view.empty()) {
        ref.view = view;
    }

    return !ref.lib.empty() && !ref.cell.empty() && !ref.view.empty();
}

bool pyDictToParamArray(PyObject *obj, oaParamArray &params)
{
    params.setNumElements(0);
    if (!obj || obj == Py_None) {
        return true;
    }
    if (!PyDict_Check(obj)) {
        return false;
    }

    oaParamArray out(0);
    PyObject *key = NULL;
    PyObject *value = NULL;
    Py_ssize_t pos = 0;
    while (PyDict_Next(obj, &pos, &key, &value)) {
        std::string name = pyToString(key);
        if (name.empty()) {
            continue;
        }

        oaParam param;
        param.setName(name.c_str());
        if (!value || value == Py_None) {
            param.setStringVal("");
        } else if (PyBool_Check(value)) {
            param.setBooleanVal(value == Py_True);
        } else if (PyLong_Check(value)) {
            param.setIntVal(static_cast<oaInt4>(pyToLong(value)));
        } else if (PyFloat_Check(value)) {
            double raw = PyFloat_AsDouble(value);
            if (PyErr_Occurred()) {
                PyErr_Clear();
                raw = 0.0;
            }
            param.setDoubleVal(static_cast<oaDouble>(raw));
        } else {
            std::string text = pyToString(value);
            param.setStringVal(text.c_str());
        }
        out.append(param);
    }

    params = out;
    return true;
}

oaOrient orientFromPy(PyObject *obj)
{
    std::string value = lower(pyToString(obj, "R0"));
    if (value == "r90") {
        return oacR90;
    }
    if (value == "r180") {
        return oacR180;
    }
    if (value == "r270") {
        return oacR270;
    }
    if (value == "my") {
        return oacMY;
    }
    if (value == "myr90") {
        return oacMYR90;
    }
    if (value == "mx") {
        return oacMX;
    }
    if (value == "mxr90") {
        return oacMXR90;
    }
    if (value != "r0") {
        logInfo("unknown orient '" + value + "', using R0");
    }
    return oacR0;
}

oaPlacementStatus placementStatusFromPy(PyObject *obj)
{
    std::string value = lower(pyToString(obj, "none"));
    if (value == "unplaced") {
        return oacUnplacedPlacementStatus;
    }
    if (value == "placed") {
        return oacPlacedPlacementStatus;
    }
    if (value == "fixed") {
        return oacFixedPlacementStatus;
    }
    if (value == "locked") {
        return oacLockedPlacementStatus;
    }
    return oacNonePlacementStatus;
}

bool pyToXY(PyObject *obj, long &x, long &y)
{
    PyRef seq(PySequence_Fast(obj, "point must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return false;
    }
    if (PySequence_Fast_GET_SIZE(seq.get()) != 2) {
        return false;
    }
    x = pyToLong(PySequence_Fast_GET_ITEM(seq.get(), 0));
    y = pyToLong(PySequence_Fast_GET_ITEM(seq.get(), 1));
    return true;
}

oaTransform transformFromPy(PyObject *item)
{
    long x = pyToLong(dictGet(item, "x"), 0);
    long y = pyToLong(dictGet(item, "y"), 0);
    PyObject *orientObj = dictGet(item, "orient");

    PyObject *origin = dictGet(item, "origin");
    if (origin) {
        pyToXY(origin, x, y);
    }

    PyObject *transform = dictGet(item, "transform");
    if (transform && PyDict_Check(transform)) {
        x = pyToLong(dictGet(transform, "x"), x);
        y = pyToLong(dictGet(transform, "y"), y);
        PyObject *nestedOrigin = dictGet(transform, "origin");
        if (nestedOrigin) {
            pyToXY(nestedOrigin, x, y);
        }
        PyObject *nestedOrient = dictGet(transform, "orient");
        if (nestedOrient) {
            orientObj = nestedOrient;
        }
    } else if (transform && transform != Py_None) {
        PyRef seq(PySequence_Fast(transform, "transform must be a sequence"));
        if (seq.get() && PySequence_Fast_GET_SIZE(seq.get()) >= 2) {
            x = pyToLong(PySequence_Fast_GET_ITEM(seq.get(), 0), x);
            y = pyToLong(PySequence_Fast_GET_ITEM(seq.get(), 1), y);
            if (PySequence_Fast_GET_SIZE(seq.get()) >= 3) {
                orientObj = PySequence_Fast_GET_ITEM(seq.get(), 2);
            }
        } else {
            PyErr_Clear();
        }
    }

    return oaTransform(static_cast<oaOffset>(x), static_cast<oaOffset>(y),
                       orientFromPy(orientObj));
}

bool resolveLayer(oaDesign *design, PyObject *obj, oaLayerNum &layerNum)
{
    oaUInt4 numeric = 0;
    if (pyToUnsignedNumber(obj, numeric)) {
        layerNum = static_cast<oaLayerNum>(numeric);
        return true;
    }

    std::string name = pyToString(obj);
    if (name.empty()) {
        return false;
    }

    oaTech *tech = design ? design->getTech() : NULL;
    if (!tech) {
        logInfo("cannot resolve layer name '" + name + "' without a bound OA tech");
        return false;
    }

    oaLayer *layer = oaLayer::find(tech, oaString(name.c_str()));
    if (!layer) {
        logInfo("unknown OA layer '" + name + "'");
        return false;
    }

    layerNum = layer->getNumber();
    return true;
}

bool resolvePurpose(oaDesign *design, PyObject *obj, oaPurposeNum &purposeNum)
{
    if (!obj || obj == Py_None) {
        purposeNum = 0;
        return true;
    }

    oaUInt4 numeric = 0;
    if (pyToUnsignedNumber(obj, numeric)) {
        purposeNum = static_cast<oaPurposeNum>(numeric);
        return true;
    }

    std::string name = pyToString(obj);
    if (name.empty()) {
        return false;
    }

    oaTech *tech = design ? design->getTech() : NULL;
    if (tech) {
        oaPurpose *purpose = oaPurpose::find(tech, oaString(name.c_str()));
        if (purpose) {
            purposeNum = purpose->getNumber();
            return true;
        }
    }

    std::string low = lower(name);
    if (low == "drawing" || low == "draw") {
        purposeNum = 0;
        return true;
    }

    logInfo("unknown OA purpose '" + name + "'");
    return false;
}

oaUInt4 accessDirFromPy(PyObject *obj)
{
    if (!obj || obj == Py_None) {
        return oacNone;
    }

    std::vector<std::string> parts;
    if (PyUnicode_Check(obj)) {
        std::string text = lower(pyToString(obj));
        std::string::size_type begin = 0;
        while (begin <= text.size()) {
            std::string::size_type end = text.find_first_of(", ", begin);
            std::string item = text.substr(begin, end == std::string::npos ? end : end - begin);
            if (!item.empty()) {
                parts.push_back(item);
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }
    } else {
        PyRef seq(PySequence_Fast(obj, "access_dir must be a string or sequence"));
        if (!seq.get()) {
            PyErr_Clear();
            return oacNone;
        }
        Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
        for (Py_ssize_t i = 0; i < n; ++i) {
            parts.push_back(lower(pyToString(PySequence_Fast_GET_ITEM(seq.get(), i))));
        }
    }

    oaUInt4 out = oacNone;
    for (std::vector<std::string>::const_iterator it = parts.begin(); it != parts.end(); ++it) {
        if (*it == "top") {
            out |= oacTop;
        } else if (*it == "bottom") {
            out |= oacBottom;
        } else if (*it == "left") {
            out |= oacLeft;
        } else if (*it == "right") {
            out |= oacRight;
        }
    }
    return out;
}

oaSigType sigTypeFromString(const std::string &raw)
{
    std::string value = lower(raw);
    if (value == "power") {
        return oacPowerSigType;
    }
    if (value == "ground") {
        return oacGroundSigType;
    }
    if (value == "clock") {
        return oacClockSigType;
    }
    if (value == "tieoff") {
        return oacTieoffSigType;
    }
    if (value == "tiehi") {
        return oacTieHiSigType;
    }
    if (value == "tielo") {
        return oacTieLoSigType;
    }
    if (value == "analog") {
        return oacAnalogSigType;
    }
    if (value == "scan") {
        return oacScanSigType;
    }
    if (value == "reset") {
        return oacResetSigType;
    }
    return oacSignalSigType;
}

oaTermType termTypeFromString(const std::string &raw)
{
    std::string value = lower(raw);
    if (value == "input" || value == "in") {
        return oacInputTermType;
    }
    if (value == "output" || value == "out") {
        return oacOutputTermType;
    }
    if (value == "switch") {
        return oacSwitchTermType;
    }
    if (value == "jumper") {
        return oacJumperTermType;
    }
    if (value == "unused") {
        return oacUnusedTermType;
    }
    if (value == "tristate") {
        return oacTristateTermType;
    }
    return oacInputOutputTermType;
}

oaBlock *ensureBlock(oaDesign *design)
{
    oaBlock *block = design ? design->getTopBlock() : NULL;
    if (!block && design) {
        block = oaBlock::create(design);
    }
    return block;
}

oaScalarNet *ensureScalarNet(oaBlock *block, oaNativeNS &ns, const std::string &name,
                             oaSigType sigType = oacSignalSigType,
                             bool isGlobal = false)
{
    if (!block || name.empty()) {
        return NULL;
    }

    oaScalarName oaName(ns, name.c_str());
    oaScalarNet *net = oaScalarNet::find(block, oaName);
    if (net) {
        return net;
    }

    try {
        return oaScalarNet::create(block, oaName, sigType, isGlobal);
    } catch (...) {
        logInfo("failed to create scalar net '" + name + "'");
        return NULL;
    }
}

oaScalarTerm *ensureScalarTerm(oaBlock *block, oaNativeNS &ns, const std::string &name,
                               const std::string &netName,
                               oaTermType termType = oacInputOutputTermType)
{
    if (!block || name.empty() || netName.empty()) {
        return NULL;
    }

    oaScalarName termName(ns, name.c_str());
    oaScalarTerm *term = oaScalarTerm::find(block, termName);
    if (term) {
        try {
            term->setTermType(termType);
        } catch (...) {
            logInfo("failed to update scalar term type for '" + name + "'");
        }
        return term;
    }

    oaScalarNet *net = ensureScalarNet(block, ns, netName);
    if (!net) {
        return NULL;
    }

    try {
        return oaScalarTerm::create(net, termName, termType);
    } catch (...) {
        logInfo("failed to create scalar term '" + name + "'");
        return NULL;
    }
}

void rememberFigName(FigureMap &figures, const std::string &name, oaFig *fig, bool pinFig)
{
    if (!fig || name.empty()) {
        return;
    }
    figures[name] = FigureRef(fig, pinFig);
}

void rememberFigFromDict(FigureMap &figures, PyObject *item, oaFig *fig, bool pinFig)
{
    rememberFigName(figures, pyToString(dictGet(item, "name")), fig, pinFig);
    rememberFigName(figures, pyToString(dictGet(item, "fig_name")), fig, pinFig);
}

void applyMustJoinTerms(oaBlock *block, oaNativeNS &ns, PyObject *items)
{
    PyRef seq(PySequence_Fast(items, "terms must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        if (!PyDict_Check(item)) {
            continue;
        }

        std::string name = pyToString(dictGet(item, "name"));
        std::string mustJoin = pyToString(dictGet(item, "must_join"));
        if (name.empty() || mustJoin.empty()) {
            continue;
        }

        oaScalarTerm *term = oaScalarTerm::find(block, oaScalarName(ns, name.c_str()));
        oaScalarTerm *base = oaScalarTerm::find(block, oaScalarName(ns, mustJoin.c_str()));
        if (!term || !base || term == base) {
            continue;
        }

        try {
            term->setMustJoin(base);
        } catch (...) {
            logInfo("failed to set must_join from '" + name + "' to '" + mustJoin + "'");
        }
    }
}

void applyNets(oaBlock *block, oaNativeNS &ns, PyObject *items)
{
    PyRef seq(PySequence_Fast(items, "nets must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        std::string name;
        std::string sig = "signal";
        bool isGlobal = false;

        if (PyDict_Check(item)) {
            name = pyToString(dictGet(item, "name"));
            PyObject *sigObj = dictGet(item, "sig_type");
            if (!sigObj) {
                sigObj = dictGet(item, "type");
            }
            sig = pyToString(sigObj, "signal");
            isGlobal = pyToBool(dictGet(item, "is_global"), false);
        } else {
            name = pyToString(item);
        }

        ensureScalarNet(block, ns, name, sigTypeFromString(sig), isGlobal);
    }
}

void applyBusNets(oaBlock *block, oaNativeNS &ns, PyObject *items)
{
    PyRef seq(PySequence_Fast(items, "busnets must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        if (!PyDict_Check(item)) {
            continue;
        }

        std::string name = pyToString(dictGet(item, "name"));
        long lowerBit = pyToLong(dictGet(item, "lower"), 0);
        long upperBit = pyToLong(dictGet(item, "upper"), 0);
        long step = pyToLong(dictGet(item, "step"), 1);
        std::string sig = pyToString(dictGet(item, "sig_type"), "signal");
        bool isGlobal = pyToBool(dictGet(item, "is_global"), false);

        if (name.empty() || lowerBit < 0 || upperBit < 0 || step <= 0) {
            logInfo("skipping invalid busnet");
            continue;
        }

        oaScalarName baseName(ns, name.c_str());
        oaBusNet *existing = oaBusNet::find(
            block, baseName, static_cast<oaUInt4>(lowerBit),
            static_cast<oaUInt4>(upperBit), static_cast<oaUInt4>(step));
        if (existing) {
            continue;
        }

        try {
            oaBusNet::create(block, baseName, static_cast<oaUInt4>(lowerBit),
                             static_cast<oaUInt4>(upperBit),
                             static_cast<oaUInt4>(step), sigTypeFromString(sig),
                             isGlobal);
        } catch (...) {
            logInfo("failed to create busnet '" + name + "'");
        }
    }
}

void applyTerms(oaBlock *block, oaNativeNS &ns, PyObject *items)
{
    PyRef seq(PySequence_Fast(items, "terms must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        if (!PyDict_Check(item)) {
            continue;
        }

        std::string name = pyToString(dictGet(item, "name"));
        std::string netName = pyToString(dictGet(item, "net"), name.c_str());
        std::string typeName = pyToString(dictGet(item, "type"), "inout");
        if (name.empty() || netName.empty()) {
            continue;
        }

        ensureScalarTerm(block, ns, name, netName, termTypeFromString(typeName));
    }
    applyMustJoinTerms(block, ns, items);
}

oaRect *createRectFromDict(oaDesign *design, oaBlock *block, PyObject *item, const char *context)
{
    oaLayerNum layerNum = 0;
    oaPurposeNum purposeNum = 0;
    if (!resolveLayer(design, dictGet(item, "layer"), layerNum) ||
        !resolvePurpose(design, dictGet(item, "purpose"), purposeNum)) {
        logInfo(std::string("skipping ") + context + " with invalid layer/purpose");
        return NULL;
    }

    oaBox box;
    if (!pyToBox(dictGet(item, "box"), box)) {
        logInfo(std::string("skipping ") + context + " with invalid box");
        return NULL;
    }

    try {
        return oaRect::create(block, layerNum, purposeNum, box);
    } catch (...) {
        logInfo(std::string("failed to create ") + context);
        return NULL;
    }
}

void applyRects(oaDesign *design, oaBlock *block, PyObject *items, FigureMap &figures)
{
    PyRef seq(PySequence_Fast(items, "rects must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        if (!PyDict_Check(item)) {
            continue;
        }

        oaRect *fig = createRectFromDict(design, block, item, "rect");
        rememberFigFromDict(figures, item, fig, false);
    }
}

void applyPaths(oaDesign *design, oaBlock *block, PyObject *items, FigureMap &figures)
{
    PyRef seq(PySequence_Fast(items, "paths must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        if (!PyDict_Check(item)) {
            continue;
        }

        oaLayerNum layerNum = 0;
        oaPurposeNum purposeNum = 0;
        if (!resolveLayer(design, dictGet(item, "layer"), layerNum) ||
            !resolvePurpose(design, dictGet(item, "purpose"), purposeNum)) {
            logInfo("skipping path with invalid layer/purpose");
            continue;
        }

        oaPointArray points;
        if (!pyToPointArray(dictGet(item, "points"), points)) {
            logInfo("skipping path with invalid points");
            continue;
        }

        try {
            oaPath *fig = oaPath::create(
                block, layerNum, purposeNum,
                static_cast<oaDist>(pyToLong(dictGet(item, "width"), 0)),
                points);
            rememberFigFromDict(figures, item, fig, false);
        } catch (...) {
            logInfo("failed to create path");
        }
    }
}

void applyPolygons(oaDesign *design, oaBlock *block, PyObject *items, FigureMap &figures)
{
    PyRef seq(PySequence_Fast(items, "polygons must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        if (!PyDict_Check(item)) {
            continue;
        }

        oaLayerNum layerNum = 0;
        oaPurposeNum purposeNum = 0;
        if (!resolveLayer(design, dictGet(item, "layer"), layerNum) ||
            !resolvePurpose(design, dictGet(item, "purpose"), purposeNum)) {
            logInfo("skipping polygon with invalid layer/purpose");
            continue;
        }

        oaPointArray points;
        if (!pyToPointArray(dictGet(item, "points"), points)) {
            logInfo("skipping polygon with invalid points");
            continue;
        }

        try {
            oaPolygon *fig = oaPolygon::create(block, layerNum, purposeNum, points);
            rememberFigFromDict(figures, item, fig, false);
        } catch (...) {
            logInfo("failed to create polygon");
        }
    }
}

void applyPins(oaDesign *design, oaBlock *block, oaNativeNS &ns, PyObject *items,
               FigureMap &figures)
{
    PyRef seq(PySequence_Fast(items, "pins must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        if (!PyDict_Check(item)) {
            continue;
        }

        std::string pinName = pyToString(dictGet(item, "name"));
        std::string termName = pyToString(dictGet(item, "term"), pinName.c_str());
        std::string netName = pyToString(dictGet(item, "net"), termName.c_str());
        std::string typeName = pyToString(dictGet(item, "type"), "inout");
        if (pinName.empty() || termName.empty() || netName.empty()) {
            logInfo("skipping pin with empty name/term/net");
            continue;
        }

        oaScalarTerm *term = ensureScalarTerm(block, ns, termName, netName,
                                              termTypeFromString(typeName));
        if (!term) {
            continue;
        }

        oaRect *fig = createRectFromDict(design, block, item, "pin rect");
        if (!fig) {
            continue;
        }

        oaString oaPinName(pinName.c_str());
        oaPin *pin = oaPin::find(term, oaPinName);
        if (!pin) {
            try {
                pin = oaPin::create(term, oaPinName, accessDirFromPy(dictGet(item, "access_dir")));
            } catch (...) {
                logInfo("failed to create pin '" + pinName + "'");
                continue;
            }
        }

        try {
            fig->addToPin(pin);
        } catch (...) {
            logInfo("failed to attach pin fig for '" + pinName + "'");
        }
        rememberFigName(figures, pinName, fig, true);
        rememberFigName(figures, pyToString(dictGet(item, "fig_name")), fig, true);
    }
}

void applyInsts(oaBlock *block, oaNativeNS &ns, PyObject *items)
{
    PyRef seq(PySequence_Fast(items, "insts must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        if (!PyDict_Check(item)) {
            continue;
        }

        std::string name = pyToString(dictGet(item, "name"));
        if (name.empty()) {
            logInfo("skipping inst with empty name");
            continue;
        }

        MasterRef master;
        if (!parseMasterRef(item, master)) {
            logInfo("skipping inst '" + name + "' with invalid master reference");
            continue;
        }

        oaParamArray params;
        PyObject *paramsObj = dictGet(item, "params");
        if (!pyDictToParamArray(paramsObj, params)) {
            logInfo("skipping inst '" + name + "' with invalid params");
            continue;
        }

        oaScalarName instName(ns, name.c_str());
        if (oaScalarInst::find(block, instName)) {
            logInfo("skipping duplicate scalar inst '" + name + "'");
            continue;
        }

        try {
            oaScalarInst::create(
                block,
                oaScalarName(ns, master.lib.c_str()),
                oaScalarName(ns, master.cell.c_str()),
                oaScalarName(ns, master.view.c_str()),
                instName,
                transformFromPy(item),
                paramsObj ? &params : NULL,
                oacInheritFromTopBlock,
                placementStatusFromPy(dictGet(item, "status")));
        } catch (...) {
            logInfo("failed to create scalar inst '" + name + "' of " +
                    master.lib + "/" + master.cell + "/" + master.view);
        }
    }
}

bool pinsToJsonArray(PyObject *obj, std::string &value)
{
    PyRef seq(PySequence_Fast(obj, "weak_join pins must be a sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return false;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    if (n < 2) {
        return false;
    }

    value = "[";
    for (Py_ssize_t i = 0; i < n; ++i) {
        if (i != 0) {
            value += ",";
        }
        value += "\"";
        value += jsonEscape(pyToString(PySequence_Fast_GET_ITEM(seq.get(), i)));
        value += "\"";
    }
    value += "]";
    return true;
}

bool pyObjectToJson(PyObject *obj, std::string &value)
{
    if (!obj) {
        return false;
    }

    PyRef jsonModule(PyImport_ImportModule("json"));
    if (!jsonModule.get()) {
        PyErr_Clear();
        return false;
    }

    PyRef dumps(PyObject_GetAttrString(jsonModule.get(), "dumps"));
    if (!dumps.get() || !PyCallable_Check(dumps.get())) {
        PyErr_Clear();
        return false;
    }

    PyRef args(PyTuple_Pack(1, obj));
    if (!args.get()) {
        PyErr_Clear();
        return false;
    }

    PyRef rendered(PyObject_CallObject(dumps.get(), args.get()));
    if (!rendered.get()) {
        PyErr_Clear();
        return false;
    }

    value = pyToString(rendered.get());
    return !value.empty();
}

bool collectStringList(PyObject *obj, std::vector<std::string> &parts)
{
    if (!obj || obj == Py_None) {
        return false;
    }

    if (PyUnicode_Check(obj)) {
        std::string text = lower(pyToString(obj));
        std::string::size_type begin = 0;
        while (begin <= text.size()) {
            std::string::size_type end = text.find_first_of(", ", begin);
            std::string item = text.substr(begin, end == std::string::npos ? end : end - begin);
            if (!item.empty()) {
                parts.push_back(item);
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }
        return !parts.empty();
    }

    PyRef seq(PySequence_Fast(obj, "value must be a string or sequence"));
    if (!seq.get()) {
        PyErr_Clear();
        return false;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        std::string item = lower(pyToString(PySequence_Fast_GET_ITEM(seq.get(), i)));
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
    return !parts.empty();
}

bool stringListToJsonArray(PyObject *obj, std::string &value)
{
    std::vector<std::string> parts;
    if (!collectStringList(obj, parts)) {
        return false;
    }

    value = "[";
    for (std::vector<std::string>::size_type i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            value += ",";
        }
        value += "\"";
        value += jsonEscape(parts[i]);
        value += "\"";
    }
    value += "]";
    return true;
}

void destroyExistingProp(oaObject *object, const std::string &name)
{
    if (!object || name.empty()) {
        return;
    }

    oaString propName(name.c_str());
    try {
        oaProp *existing = oaProp::find(object, propName);
        if (existing) {
            existing->destroy();
        }
    } catch (...) {
        logInfo("failed to replace prop '" + name + "'");
    }
}

void setStringProp(oaObject *object, const std::string &name, const std::string &value)
{
    if (!object || name.empty()) {
        return;
    }

    oaString propName(name.c_str());
    oaString propValue(value.c_str());
    try {
        destroyExistingProp(object, name);
        oaStringProp::create(object, propName, propValue);
    } catch (...) {
        logInfo("failed to write string prop '" + name + "'");
    }
}

void setBooleanProp(oaObject *object, const std::string &name, bool value)
{
    if (!object || name.empty()) {
        return;
    }

    try {
        destroyExistingProp(object, name);
        oaBooleanProp::create(object, oaString(name.c_str()), value);
    } catch (...) {
        logInfo("failed to write boolean prop '" + name + "'");
    }
}

void setIntProp(oaObject *object, const std::string &name, long value)
{
    if (!object || name.empty()) {
        return;
    }

    try {
        destroyExistingProp(object, name);
        oaIntProp::create(object, oaString(name.c_str()), static_cast<oaInt4>(value));
    } catch (...) {
        logInfo("failed to write int prop '" + name + "'");
    }
}

void setDoubleProp(oaObject *object, const std::string &name, double value)
{
    if (!object || name.empty()) {
        return;
    }

    try {
        destroyExistingProp(object, name);
        oaDoubleProp::create(object, oaString(name.c_str()), static_cast<oaDouble>(value));
    } catch (...) {
        logInfo("failed to write double prop '" + name + "'");
    }
}

void setFloatProp(oaObject *object, const std::string &name, double value)
{
    if (!object || name.empty()) {
        return;
    }

    try {
        destroyExistingProp(object, name);
        oaFloatProp::create(object, oaString(name.c_str()), static_cast<oaFloat>(value));
    } catch (...) {
        logInfo("failed to write float prop '" + name + "'");
    }
}

void setFloatOrStringProp(oaObject *object, const std::string &name, PyObject *value)
{
    if (!value || value == Py_None) {
        return;
    }
    if (PyUnicode_Check(value)) {
        setStringProp(object, name, pyToString(value));
    } else {
        setFloatProp(object, name, pyToDouble(value, 0.0));
    }
}

void setTypedPropFromPy(oaObject *object, const std::string &name, PyObject *value)
{
    if (!object || name.empty() || !value || value == Py_None) {
        return;
    }

    if (PyBool_Check(value)) {
        setBooleanProp(object, name, value == Py_True);
    } else if (PyLong_Check(value)) {
        setIntProp(object, name, pyToLong(value));
    } else if (PyFloat_Check(value)) {
        setDoubleProp(object, name, pyToDouble(value));
    } else if (PyUnicode_Check(value)) {
        setStringProp(object, name, pyToString(value));
    } else {
        std::string json;
        if (pyObjectToJson(value, json)) {
            setStringProp(object, name, json);
        } else {
            setStringProp(object, name, pyToString(value));
        }
    }
}

void applyWeakJoinGroup(oaBlock *block, const std::string &name, PyObject *pinsObj)
{
    std::string pinsJson;
    if (name.empty() || !pinsToJsonArray(pinsObj, pinsJson)) {
        logInfo("skipping invalid weak_join group");
        return;
    }

    setStringProp(block, "acell.weak_join." + propSafeSuffix(name), pinsJson);
}

void applyWeakJoins(oaBlock *block, PyObject *items)
{
    if (!items || items == Py_None) {
        return;
    }

    if (PyDict_Check(items)) {
        PyObject *key = NULL;
        PyObject *value = NULL;
        Py_ssize_t pos = 0;
        while (PyDict_Next(items, &pos, &key, &value)) {
            applyWeakJoinGroup(block, pyToString(key), value);
        }
        return;
    }

    PyRef seq(PySequence_Fast(items, "weak_joins must be a sequence or dict"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq.get(), i);
        if (!PyDict_Check(item)) {
            continue;
        }

        std::string name = pyToString(dictGet(item, "name"));
        if (name.empty()) {
            name = pyToString(dictGet(item, "group"));
        }
        applyWeakJoinGroup(block, name, dictGet(item, "pins"));
    }
}

void setStringPropFromDict(oaObject *object, PyObject *item, const std::string &propName,
                           const char *a, const char *b = NULL, const char *c = NULL)
{
    std::string value = pyToString(dictGetAlias(item, a, b, c));
    if (!value.empty()) {
        setStringProp(object, propName, value);
    }
}

void setFloatPropFromDict(oaObject *object, PyObject *item, const std::string &propName,
                          const char *a, const char *b = NULL, const char *c = NULL)
{
    PyObject *value = dictGetAlias(item, a, b, c);
    if (value && value != Py_None) {
        setFloatProp(object, propName, pyToDouble(value, 0.0));
    }
}

void setBooleanPropFromDict(oaObject *object, PyObject *item, const std::string &propName,
                            const char *a, const char *b = NULL, const char *c = NULL)
{
    PyObject *value = dictGetAlias(item, a, b, c);
    if (value && value != Py_None) {
        setBooleanProp(object, propName, pyToBool(value, false));
    }
}

void setDirectionListPropFromDict(oaObject *object, PyObject *item,
                                  const std::string &propName,
                                  const char *a, const char *b = NULL,
                                  const char *c = NULL)
{
    PyObject *value = dictGetAlias(item, a, b, c);
    std::string json;
    if (value && stringListToJsonArray(value, json)) {
        setStringProp(object, propName, json);
    }
}

void setJsonPropFromDict(oaObject *object, PyObject *item, const std::string &propName,
                         const char *a, const char *b = NULL, const char *c = NULL)
{
    PyObject *value = dictGetAlias(item, a, b, c);
    std::string json;
    if (value && value != Py_None && pyObjectToJson(value, json)) {
        setStringProp(object, propName, json);
    }
}

void applyExtraProps(oaObject *object, PyObject *props)
{
    if (!props || !PyDict_Check(props)) {
        return;
    }

    PyObject *key = NULL;
    PyObject *value = NULL;
    Py_ssize_t pos = 0;
    while (PyDict_Next(props, &pos, &key, &value)) {
        std::string name = pyToString(key);
        if (!name.empty()) {
            setTypedPropFromPy(object, name, value);
        }
    }
}

void applyAbutmentItem(const FigureMap &figures, const std::string &defaultTarget,
                       PyObject *item)
{
    if (!PyDict_Check(item)) {
        return;
    }

    std::string target = pyToString(dictGetAlias(item, "target", "fig", "name"),
                                    defaultTarget.c_str());
    if (target.empty()) {
        target = pyToString(dictGetAlias(item, "fig_name", "abut_fig_name", "abutFigName"));
    }
    if (target.empty()) {
        target = pyToString(dictGetAlias(item, "pin_fig_name", "pinFigName"));
    }

    FigureMap::const_iterator found = figures.find(target);
    if (target.empty() || found == figures.end() || !found->second.fig) {
        logInfo("skipping abutment with unknown target '" + target + "'");
        return;
    }

    oaFig *fig = found->second.fig;
    setDirectionListPropFromDict(fig, item, "abutAccessDir", "access_dir", "abutAccessDir");
    setStringPropFromDict(fig, item, "abutFunction", "function", "abut_function", "abutFunction");
    setStringPropFromDict(fig, item, "abutParam", "param", "abut_param", "abutParam");
    setStringPropFromDict(fig, item, "abutClass", "abut_class", "class", "abutClass");
    setFloatPropFromDict(fig, item, "abutOffset", "offset", "abut_offset", "abutOffset");
    setDirectionListPropFromDict(fig, item, "vxlInstSpacingDir", "spacing_dir",
                                 "vxlInstSpacingDir");
    setFloatOrStringProp(fig, "vxlInstSpacingRule",
                         dictGetAlias(item, "spacing_rule", "vxlInstSpacingRule"));
    setBooleanPropFromDict(fig, item, "lxAutoAbut", "auto_abut", "lxAutoAbut");
    setBooleanPropFromDict(fig, item, "lxAutoSpace", "auto_space", "lxAutoSpace");
    setStringPropFromDict(fig, item, "permuteRule", "permute_rule", "permuteRule");
    setFloatPropFromDict(fig, item, "gateWidth", "gate_width", "gateWidth");
    setBooleanPropFromDict(fig, item, "isSource", "is_source", "isSource");

    std::string abutFigName = pyToString(dictGetAlias(item, "fig_name", "abut_fig_name",
                                                       "abutFigName"));
    std::string pinFigName = pyToString(dictGetAlias(item, "pin_fig_name", "pinFigName"));
    if (abutFigName.empty() && pinFigName.empty()) {
        if (found->second.pinFig) {
            pinFigName = target;
        } else {
            abutFigName = target;
        }
    }
    if (!abutFigName.empty()) {
        setStringProp(fig, "abutFigName", abutFigName);
    }
    if (!pinFigName.empty()) {
        setStringProp(fig, "pinFigName", pinFigName);
    }

    setStringPropFromDict(fig, item, "acell.abut_event.handler",
                          "event_handler", "handler", "abut_event_handler");
    setStringPropFromDict(fig, item, "acell.abut_event.module",
                          "event_module", "abut_event_module");
    setStringPropFromDict(fig, item, "acell.abut_event.func",
                          "event_func", "abut_event_func");
    setStringPropFromDict(fig, item, "acell.abut_event.schema",
                          "event_schema", "abut_event_schema");
    setJsonPropFromDict(fig, item, "acell.abut_event.events",
                        "events", "abut_events");

    applyExtraProps(fig, dictGetAlias(item, "props", "properties"));

    std::string payload;
    if (pyObjectToJson(item, payload)) {
        setStringProp(fig, "acell.abutment", payload);
    }
}

void applyAbutments(PyObject *items, const FigureMap &figures)
{
    if (!items || items == Py_None) {
        return;
    }

    if (PyDict_Check(items)) {
        if (dictGet(items, "target")) {
            applyAbutmentItem(figures, "", items);
            return;
        }

        PyObject *key = NULL;
        PyObject *value = NULL;
        Py_ssize_t pos = 0;
        while (PyDict_Next(items, &pos, &key, &value)) {
            applyAbutmentItem(figures, pyToString(key), value);
        }
        return;
    }

    PyRef seq(PySequence_Fast(items, "abutments must be a sequence or dict"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        applyAbutmentItem(figures, "", PySequence_Fast_GET_ITEM(seq.get(), i));
    }
}

void applyStretchItem(const FigureMap &figures, const std::string &defaultTarget,
                      PyObject *item)
{
    if (!PyDict_Check(item)) {
        return;
    }

    std::string target = pyToString(dictGet(item, "target"), defaultTarget.c_str());
    FigureMap::const_iterator found = figures.find(target);
    if (target.empty() || found == figures.end() || !found->second.fig) {
        logInfo("skipping stretch with unknown target '" + target + "'");
        return;
    }

    std::string parameter = pyToString(dictGet(item, "parameter"));
    std::string anchor = pyToString(dictGet(item, "anchor"), "handle");
    std::string suffix = !parameter.empty() ? parameter : anchor;
    std::string payload;
    if (pyObjectToJson(item, payload)) {
        setStringProp(found->second.fig, "acell.stretch." + propSafeSuffix(suffix), payload);
    }
}

void applyStretches(PyObject *items, const FigureMap &figures)
{
    if (!items || items == Py_None) {
        return;
    }

    if (PyDict_Check(items)) {
        if (dictGet(items, "target")) {
            applyStretchItem(figures, "", items);
            return;
        }
        PyObject *key = NULL;
        PyObject *value = NULL;
        Py_ssize_t pos = 0;
        while (PyDict_Next(items, &pos, &key, &value)) {
            applyStretchItem(figures, pyToString(key), value);
        }
        return;
    }

    PyRef seq(PySequence_Fast(items, "stretches must be a sequence or dict"));
    if (!seq.get()) {
        PyErr_Clear();
        return;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq.get());
    for (Py_ssize_t i = 0; i < n; ++i) {
        applyStretchItem(figures, "", PySequence_Fast_GET_ITEM(seq.get(), i));
    }
}

void applyGeometry(oaDesign *design, PyObject *result)
{
    if (!PyDict_Check(result)) {
        logInfo("Python evaluator returned non-dict result; skipping geometry");
        return;
    }

    oaBlock *block = ensureBlock(design);
    if (!block) {
        logInfo("no OA block available for geometry");
        return;
    }

    oaNativeNS ns;
    FigureMap figures;

    PyObject *items = dictGet(result, "nets");
    if (items) {
        applyNets(block, ns, items);
    }

    items = dictGet(result, "busnets");
    if (items) {
        applyBusNets(block, ns, items);
    }

    items = dictGet(result, "terms");
    if (items) {
        applyTerms(block, ns, items);
    }

    items = dictGet(result, "rects");
    if (items) {
        applyRects(design, block, items, figures);
    }

    items = dictGet(result, "paths");
    if (items) {
        applyPaths(design, block, items, figures);
    }

    items = dictGet(result, "polygons");
    if (items) {
        applyPolygons(design, block, items, figures);
    }

    items = dictGet(result, "pins");
    if (items) {
        applyPins(design, block, ns, items, figures);
    }

    PyObject *abutments = dictGet(result, "abutments");
    if (abutments) {
        applyAbutments(abutments, figures);
    }

    PyObject *stretches = dictGet(result, "stretches");
    if (stretches) {
        applyStretches(stretches, figures);
    }

    PyObject *insts = dictGet(result, "insts");
    if (insts) {
        applyInsts(block, ns, insts);
    }

    PyObject *weakJoins = dictGet(result, "weak_joins");
    if (weakJoins) {
        applyWeakJoins(block, weakJoins);
    }
}

} // namespace

class PcellDef : public oaPcellDef {
public:
    explicit PcellDef(IPcell *pcell) : oaPcellDef(pcell) {}
};

class AiviPcell : public PlugInBase<IPcell> {
public:
    AiviPcell() : pcd_(NULL) {}
    ~AiviPcell() override
    {
        delete pcd_;
        pcd_ = NULL;
    }

    oaPcellDef *getPcellDef() override
    {
        if (!pcd_) {
            pcd_ = new PcellDef(this);
        }
        return pcd_;
    }

    void getName(oaString &name) override { name = pluginName; }

    void onBind(oaDesign *, oaPcellDef *) override {}

    void onUnbind(oaDesign *, oaPcellDef *def) override
    {
        if (def == pcd_) {
            pcd_ = NULL;
        }
        delete def;
    }

    void onEval(oaDesign *design, oaPcellDef *pcellDef) override
    {
        if (!design || !pcellDef) {
            return;
        }

        normalizeMetadata(pcellDef);
        oaString moduleName = getModule(pcellDef);
        oaString funcName = getFunc(pcellDef);
        if (moduleName.getLength() == 0) {
            logInfo("AcellModule/PyModule is not set on PcellDef");
            ensureBlock(design);
            return;
        }
        ensureBlock(design);

        ensurePython();
        PyGILState_STATE gil = PyGILState_Ensure();

        PyRef module(PyImport_ImportModule(static_cast<const char *>(moduleName)));
        if (!module.get()) {
            logPyError("cannot import Python module '" + oaToString(moduleName) + "'");
            PyGILState_Release(gil);
            ensureBlock(design);
            return;
        }

        PyRef func(PyObject_GetAttrString(module.get(), static_cast<const char *>(funcName)));
        if (!func.get() || !PyCallable_Check(func.get())) {
            logPyError("cannot find callable '" + oaToString(funcName) + "'");
            PyGILState_Release(gil);
            ensureBlock(design);
            return;
        }

        PyRef params(paramsToPyDict(design));
        if (!params.get()) {
            logPyError("cannot convert OA params to Python dict");
            PyGILState_Release(gil);
            ensureBlock(design);
            return;
        }

        PyRef runtime(runtimeToPyDict(design));
        if (!runtime.get()) {
            logPyError("cannot create Python runtime context");
            PyGILState_Release(gil);
            ensureBlock(design);
            return;
        }

        PyRef result(callEvaluator(func.get(), params.get(), runtime.get()));
        if (!result.get()) {
            logPyError("Python evaluator raised an exception");
            PyGILState_Release(gil);
            ensureBlock(design);
            return;
        }

        applyGeometry(design, result.get());
        PyGILState_Release(gil);
    }

    void onRead(oaDesign *, oaMapFileWindow &mapWindow, oaUInt4 &loc,
                oaPcellDef *def) override
    {
        oaUInt4 schema = 0;
        oaUInt4 count = 0;
        mapWindow.data().readUInt4(loc, schema);
        mapWindow.data().readUInt4(loc, count);

        if (schema != kDiskSchema) {
            logInfo("unsupported PcellDef disk schema; metadata not restored");
            return;
        }

        for (oaUInt4 i = 0; i < count; ++i) {
            oaString key;
            oaString value;
            mapWindow.data().readString(loc, key);
            mapWindow.data().readString(loc, value);
            if (key.getLength() != 0) {
                setData(def, static_cast<const char *>(key), value);
            }
        }
        normalizeMetadata(def);
    }

    void onWrite(oaDesign *, oaMapFileWindow &mapWindow, oaUInt4 &loc,
                 oaPcellDef *def) override
    {
        std::vector<MetadataPair> pairs = collectMetadata(def, true);
        mapWindow.data().writeUInt4(loc, kDiskSchema);
        mapWindow.data().writeUInt4(loc, static_cast<oaUInt4>(pairs.size()));
        for (std::vector<MetadataPair>::const_iterator it = pairs.begin(); it != pairs.end(); ++it) {
            mapWindow.data().writeString(loc, oaString(it->key));
            mapWindow.data().writeString(loc, it->value);
        }
    }

    oaUInt4 calcDiskSize(oaPcellDef *pcellDef) const override
    {
        std::vector<MetadataPair> pairs = collectMetadata(pcellDef, false);
        oaUInt4 size = sizeof(oaUInt4) * 2;
        for (std::vector<MetadataPair>::const_iterator it = pairs.begin(); it != pairs.end(); ++it) {
            size += stringDiskSize(oaString(it->key));
            size += stringDiskSize(it->value);
        }
        return size;
    }

    static oaString pluginName;
    static Factory<AiviPcell> factory;
    static Factory<AiviPcell> legacyFactory;

private:
    PcellDef *pcd_;
};

oaString AiviPcell::pluginName(kClassId);
Factory<AiviPcell> AiviPcell::factory(kClassId);
Factory<AiviPcell> AiviPcell::legacyFactory(kLegacyClassId);

} // namespace oaAiviPcell

extern "C" long getClassObject(const char *cid, const oaCommon::Guid &iid, void **inst)
{
    return oaCommon::FactoryBase::getClassObject(cid, iid, inst);
}
