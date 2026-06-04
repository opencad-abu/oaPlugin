// *****************************************************************************
// oaDMTurboStorage.h — lib.xml + 编号文件存储管理
//
// oaDMTurbo 存储格式:
//   lib.xml          — 索引文件 (Lib/Cell/View/CellView/File 层级)
//   f000000001       — 编号数据文件
//   f000000002       — ...
// *****************************************************************************

#ifndef OADMTURBO_STORAGE_H
#define OADMTURBO_STORAGE_H

#include <string>
#include <map>
#include <vector>

namespace oaDMTurbo {

// ============================================================================
// 文件编号分配器
// ============================================================================
class FileNumAllocator {
public:
    FileNumAllocator() : nextNum_(0) {}
    
    unsigned int allocate() {
        return nextNum_++;
    }
    
    void reset() {
        nextNum_ = 0;
    }
    
    unsigned int getNext() const {
        return nextNum_;
    }
    
    void setNext(unsigned int n) {
        nextNum_ = n;
    }
    
private:
    unsigned int nextNum_;
};

// ============================================================================
// 文件记录 (对应 lib.xml 中的 <File>)
// ============================================================================
struct FileRecord {
    std::string name;
    unsigned int num;
    bool primary;
    
    FileRecord(const std::string& n, unsigned int id, bool p = false)
        : name(n), num(id), primary(p) {}
};

// ============================================================================
// CellView 记录
// ============================================================================
struct CellViewRecord {
    std::string viewName;
    std::string viewType;
    std::vector<FileRecord> files;
    
    CellViewRecord(const std::string& vn, const std::string& vt)
        : viewName(vn), viewType(vt) {}
};

// ============================================================================
// Cell 记录
// ============================================================================
struct CellRecord {
    std::string name;
    std::vector<FileRecord> files;
    std::vector<CellViewRecord> cellViews;
    
    CellRecord(const std::string& n) : name(n) {}
};

// ============================================================================
// View 记录
// ============================================================================
struct ViewRecord {
    std::string name;
    std::string type;
    std::vector<FileRecord> files;
    
    ViewRecord(const std::string& n, const std::string& t) : name(n), type(t) {}
};

// ============================================================================
// Lib 存储管理器
// ============================================================================
class LibStorage {
public:
    LibStorage(const std::string& name, const std::string& path)
        : name_(name), path_(path) {}
    
    // Cell 操作
    void addCell(const std::string& cellName);
    bool hasCell(const std::string& cellName) const;
    CellRecord* getCell(const std::string& cellName);
    
    // View 操作
    void addView(const std::string& viewName, const std::string& viewType);
    bool hasView(const std::string& viewName) const;
    
    // CellView 操作
    void addCellView(const std::string& cellName, const std::string& viewName, 
                     const std::string& viewType);
    bool hasCellView(const std::string& cellName, const std::string& viewName) const;
    CellViewRecord* getCellView(const std::string& cellName, const std::string& viewName);
    
    // File 操作
    unsigned int addFile(const std::string& parentType, const std::string& parentName,
                         const std::string& fileName, bool primary = false);
    
    // 根据文件名查找文件记录（返回 num，未找到返回 -1）
    int findFile(const std::string& fileName) const;
    
    // 根据文件名查找文件记录（返回 FileRecord 指针）
    const FileRecord* findFileRecord(const std::string& fileName) const;
    
    // 序列化到 lib.xml
    bool writeLibXml() const;
    
    // 从 lib.xml 加载
    bool loadLibXml();
    
    // 获取编号文件路径
    std::string getFilePath(unsigned int num) const;
    
    const std::string& getName() const { return name_; }
    const std::string& getPath() const { return path_; }
    
    const std::vector<CellRecord>& getCells() const { return cells_; }
    const std::vector<ViewRecord>& getViews() const { return views_; }
    const std::vector<FileRecord>& getFiles() const { return files_; }
    
private:
    std::string name_;
    std::string path_;
    
    std::vector<CellRecord> cells_;
    std::vector<ViewRecord> views_;
    std::vector<FileRecord> files_;  // Lib 级别文件
    
    FileNumAllocator fileNumAllocator_;
};

} // namespace oaDMTurbo

#endif // OADMTURBO_STORAGE_H
