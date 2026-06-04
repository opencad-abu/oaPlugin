// *****************************************************************************
// oaAiviDMTurboStorage.cpp — lib.xml storage management implementation
// *****************************************************************************

#include "oaDMTurboStorage.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace oaDMTurbo {

// ============================================================================
// LibStorage 实现
// ============================================================================

void LibStorage::addCell(const std::string& cellName) {
    if (!hasCell(cellName)) {
        cells_.push_back(CellRecord(cellName));
    }
}

bool LibStorage::hasCell(const std::string& cellName) const {
    for (size_t i = 0; i < cells_.size(); i++) {
        if (cells_[i].name == cellName) return true;
    }
    return false;
}

CellRecord* LibStorage::getCell(const std::string& cellName) {
    for (size_t i = 0; i < cells_.size(); i++) {
        if (cells_[i].name == cellName) return &cells_[i];
    }
    return nullptr;
}

void LibStorage::addView(const std::string& viewName, const std::string& viewType) {
    if (!hasView(viewName)) {
        views_.push_back(ViewRecord(viewName, viewType));
    }
}

bool LibStorage::hasView(const std::string& viewName) const {
    for (size_t i = 0; i < views_.size(); i++) {
        if (views_[i].name == viewName) return true;
    }
    return false;
}

void LibStorage::addCellView(const std::string& cellName, const std::string& viewName,
                              const std::string& viewType) {
    addCell(cellName);
    CellRecord* cell = getCell(cellName);
    if (cell && !hasCellView(cellName, viewName)) {
        cell->cellViews.push_back(CellViewRecord(viewName, viewType));
    }
}

bool LibStorage::hasCellView(const std::string& cellName, const std::string& viewName) const {
    for (size_t i = 0; i < cells_.size(); i++) {
        if (cells_[i].name == cellName) {
            for (size_t j = 0; j < cells_[i].cellViews.size(); j++) {
                if (cells_[i].cellViews[j].viewName == viewName) return true;
            }
        }
    }
    return false;
}

CellViewRecord* LibStorage::getCellView(const std::string& cellName, const std::string& viewName) {
    CellRecord* cell = getCell(cellName);
    if (cell) {
        for (size_t i = 0; i < cell->cellViews.size(); i++) {
            if (cell->cellViews[i].viewName == viewName) {
                return &cell->cellViews[i];
            }
        }
    }
    return nullptr;
}

unsigned int LibStorage::addFile(const std::string& parentType, const std::string& parentName,
                                  const std::string& fileName, bool primary) {
    unsigned int num = fileNumAllocator_.allocate();
    FileRecord rec(fileName, num, primary);
    
    if (parentType == "lib") {
        files_.push_back(rec);
    } else if (parentType == "cell") {
        CellRecord* cell = getCell(parentName);
        if (cell) cell->files.push_back(rec);
    } else if (parentType == "cellview") {
        // parentName = "cellName/viewName"
        size_t slash = parentName.find('/');
        if (slash != std::string::npos) {
            std::string cellName = parentName.substr(0, slash);
            std::string viewName = parentName.substr(slash + 1);
            CellViewRecord* cv = getCellView(cellName, viewName);
            if (cv) cv->files.push_back(rec);
        }
    } else if (parentType == "view") {
        for (size_t i = 0; i < views_.size(); i++) {
            if (views_[i].name == parentName) {
                views_[i].files.push_back(rec);
                break;
            }
        }
    }
    
    return num;
}

int LibStorage::findFile(const std::string& fileName) const {
    // 搜索所有层级的文件
    for (size_t i = 0; i < files_.size(); i++) {
        if (files_[i].name == fileName) return files_[i].num;
    }
    for (size_t i = 0; i < cells_.size(); i++) {
        for (size_t j = 0; j < cells_[i].files.size(); j++) {
            if (cells_[i].files[j].name == fileName) return cells_[i].files[j].num;
        }
        for (size_t j = 0; j < cells_[i].cellViews.size(); j++) {
            for (size_t k = 0; k < cells_[i].cellViews[j].files.size(); k++) {
                if (cells_[i].cellViews[j].files[k].name == fileName)
                    return cells_[i].cellViews[j].files[k].num;
            }
        }
    }
    for (size_t i = 0; i < views_.size(); i++) {
        for (size_t j = 0; j < views_[i].files.size(); j++) {
            if (views_[i].files[j].name == fileName) return views_[i].files[j].num;
        }
    }
    return -1;
}

const FileRecord* LibStorage::findFileRecord(const std::string& fileName) const {
    for (size_t i = 0; i < files_.size(); i++) {
        if (files_[i].name == fileName) return &files_[i];
    }
    for (size_t i = 0; i < cells_.size(); i++) {
        for (size_t j = 0; j < cells_[i].files.size(); j++) {
            if (cells_[i].files[j].name == fileName) return &cells_[i].files[j];
        }
        for (size_t j = 0; j < cells_[i].cellViews.size(); j++) {
            for (size_t k = 0; k < cells_[i].cellViews[j].files.size(); k++) {
                if (cells_[i].cellViews[j].files[k].name == fileName)
                    return &cells_[i].cellViews[j].files[k];
            }
        }
    }
    for (size_t i = 0; i < views_.size(); i++) {
        for (size_t j = 0; j < views_[i].files.size(); j++) {
            if (views_[i].files[j].name == fileName) return &views_[i].files[j];
        }
    }
    return nullptr;
}

std::string LibStorage::getFilePath(unsigned int num) const {
    char buf[32];
    snprintf(buf, sizeof(buf), "f%09u", num);
    return path_ + "/" + buf;
}

// XML 转义
static std::string xmlEscape(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else if (c == '"') result += "&quot;";
        else result += c;
    }
    return result;
}

bool LibStorage::writeLibXml() const {
    std::string xmlPath = path_ + "/lib.xml";
    FILE* f = fopen(xmlPath.c_str(), "w");
    if (!f) return false;
    
    fprintf(f, "<?xml version=\"2.0\"?>\n\n");
    fprintf(f, "<Lib name=\"%s\" path=\"%s\">\n",
            xmlEscape(name_).c_str(), xmlEscape(path_).c_str());
    
    // Lib 级别文件
    for (size_t i = 0; i < files_.size(); i++) {
        fprintf(f, "    <File name=\"%s\" num=\"%u\"/>\n",
                xmlEscape(files_[i].name).c_str(), files_[i].num);
    }
    
    // Views
    for (size_t i = 0; i < views_.size(); i++) {
        fprintf(f, "    <View name=\"%s\" type=\"%s\">\n",
                xmlEscape(views_[i].name).c_str(),
                xmlEscape(views_[i].type).c_str());
        for (size_t j = 0; j < views_[i].files.size(); j++) {
            fprintf(f, "        <File name=\"%s\" num=\"%u\"/>\n",
                    xmlEscape(views_[i].files[j].name).c_str(),
                    views_[i].files[j].num);
        }
        fprintf(f, "    </View>\n");
    }
    
    // Cells
    for (size_t i = 0; i < cells_.size(); i++) {
        fprintf(f, "    <Cell name=\"%s\">\n",
                xmlEscape(cells_[i].name).c_str());
        
        // Cell 级别文件
        for (size_t j = 0; j < cells_[i].files.size(); j++) {
            fprintf(f, "        <File name=\"%s\" num=\"%u\"/>\n",
                    xmlEscape(cells_[i].files[j].name).c_str(),
                    cells_[i].files[j].num);
        }
        
        // CellViews
        for (size_t j = 0; j < cells_[i].cellViews.size(); j++) {
            const CellViewRecord& cv = cells_[i].cellViews[j];
            fprintf(f, "        <CellView view=\"%s\" type=\"%s\">\n",
                    xmlEscape(cv.viewName).c_str(),
                    xmlEscape(cv.viewType).c_str());
            
            for (size_t k = 0; k < cv.files.size(); k++) {
                fprintf(f, "            <File name=\"%s\" num=\"%u\"%s/>\n",
                        xmlEscape(cv.files[k].name).c_str(),
                        cv.files[k].num,
                        cv.files[k].primary ? " primary=\"true\"" : "");
            }
            
            fprintf(f, "        </CellView>\n");
        }
        
        fprintf(f, "    </Cell>\n");
    }
    
    fprintf(f, "</Lib>\n");
    fclose(f);
    return true;
}

bool LibStorage::loadLibXml() {
    std::string xmlPath = path_ + "/lib.xml";
    FILE* f = fopen(xmlPath.c_str(), "r");
    if (!f) return false;
    
    // 读取整个文件
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = new char[sz + 1];
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    
    // 简单解析 <File name="xxx" num="N"/> 条目恢复文件记录
    const char* p = buf;
    unsigned int maxNum = 0;
    while ((p = strstr(p, "<File")) != nullptr) {
        const char* ns = strstr(p, "name=\"");
        const char* nump = strstr(p, "num=\"");
        if (ns && nump) {
            ns += 6; // 跳过 name="
            const char* ne = strchr(ns, '"');
            nump += 5; // 跳过 num="
            unsigned int n = 0;
            sscanf(nump, "%u", &n);
            if (ne && n < 1000000) {
                std::string fname(ns, ne - ns);
                files_.push_back(FileRecord(fname, n, strstr(p, "primary") != nullptr));
                if (n > maxNum) maxNum = n;
            }
        }
        p++; // 移到下一个字符避免死循环
    }
    delete[] buf;
    
    // 恢复文件编号分配器
    fileNumAllocator_.setNext(maxNum + 1);
    return true;
}

} // namespace oaDMTurbo
