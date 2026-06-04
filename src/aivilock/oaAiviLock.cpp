// oaAiviLock.cpp — Thread synchronization (pthread-based) for the lock plug-in bridge
#define EXPORT __attribute__((visibility("default")))

#include <pthread.h>
#include <string>
#include <cstring>

struct mtuMutex         { pthread_mutex_t m;     mtuMutex() { pthread_mutex_init(&m,0); } ~mtuMutex() { pthread_mutex_destroy(&m); } };
struct mtuSpinMutex     { pthread_spinlock_t s;  mtuSpinMutex() { pthread_spin_init(&s,0); } ~mtuSpinMutex() { pthread_spin_destroy(&s); } };
struct mtuReadWriteLock { pthread_rwlock_t rw;   mtuReadWriteLock() { pthread_rwlock_init(&rw,0); } ~mtuReadWriteLock() { pthread_rwlock_destroy(&rw); } };

namespace OpenAccess_4 {
    enum oaLockResourceTypeEnum { oacUnspecifiedResType = 0 };
    struct oaRWLock { enum oaRWLockTypeEnum { oacReadLock=0, oacWriteLock=1 }; };
    struct oaMutex { virtual ~oaMutex(); virtual void lock()=0; virtual void unlock()=0; };
    struct oaSpinMutex { virtual ~oaSpinMutex(); virtual void lock()=0; virtual void unlock()=0; };
}
namespace oaCommon {
    struct Guid { unsigned int data1=0; unsigned short data2=0,data3=0; unsigned char data4[8]={}; };
    struct IBase { virtual ~IBase() {} };
}

namespace AiviLock {

struct ILockFactoryMgr {
    enum FactoryType { cDefaultFactory=0, cRecordingFactory=1 };
    enum LockUseModelEnum { cDefaultModel=0 };
    virtual ~ILockFactoryMgr() {}
};

// ======== MutexBase ========
struct EXPORT MutexBase {
    MutexBase();
    explicit MutexBase(int);
    virtual ~MutexBase();
    mtuMutex* mtx_;
    bool enabled_;
};

// ======== Mutex ========
struct EXPORT Mutex : public OpenAccess_4::oaMutex, public MutexBase {
    explicit Mutex(mtuMutex* m = nullptr);
    ~Mutex();
    void lock() override;
    void unlock() override;
};

// ======== SpinMutex ========
struct EXPORT SpinMutex : public OpenAccess_4::oaSpinMutex {
    explicit SpinMutex(mtuSpinMutex* s = nullptr);
    ~SpinMutex();
    void lock() override;
    void unlock() override;
    mtuSpinMutex* smtx_;
    bool enabled_;
};

// ======== RWLock ========
struct EXPORT RWLock : public MutexBase {
    explicit RWLock(mtuReadWriteLock* rw = nullptr);
    ~RWLock();
    void lock(OpenAccess_4::oaRWLock::oaRWLockTypeEnum t);
    void unlock(OpenAccess_4::oaRWLock::oaRWLockTypeEnum);
    mtuReadWriteLock* rwtx_;
    bool enabled_;
};

// ======== RecordingBase ========
struct EXPORT RecordingBase {
    RecordingBase();
    virtual ~RecordingBase();
};

// ======== RecordingBaseMutex ========
struct EXPORT RecordingBaseMutex : public RecordingBase {
    RecordingBaseMutex();
    RecordingBaseMutex(unsigned int id, OpenAccess_4::oaLockResourceTypeEnum rt);
    virtual ~RecordingBaseMutex();
    unsigned int id_;
    OpenAccess_4::oaLockResourceTypeEnum resType_;
};

// ======== Recording variants ========
struct EXPORT RecordingMutex : public Mutex, public RecordingBaseMutex {
    RecordingMutex();
    RecordingMutex(mtuMutex* m, unsigned int id, OpenAccess_4::oaLockResourceTypeEnum rt);
    ~RecordingMutex();
};

struct EXPORT RecordingSpinMutex : public SpinMutex, public RecordingBaseMutex {
    RecordingSpinMutex();
    RecordingSpinMutex(mtuSpinMutex* s, unsigned int id, OpenAccess_4::oaLockResourceTypeEnum rt);
    ~RecordingSpinMutex();
};

struct EXPORT RecordingRWLock : public RWLock, public RecordingBaseMutex {
    RecordingRWLock();
    RecordingRWLock(mtuReadWriteLock* rw, unsigned int id, OpenAccess_4::oaLockResourceTypeEnum rt);
    ~RecordingRWLock();
};

// ======== ThreadRecord/ThreadRecording ========
struct ThreadRecord {};
struct EXPORT ThreadRecording {
    explicit ThreadRecording(const char*);
    ~ThreadRecording();
    void reset(const char*);
    bool isRecordingReady() const;
    void record(ThreadRecord*);
    void setRecordRelease(int);
    int getRecordRelease();
};

// ======== LockFactory (template — explicit instantiations below) ========
template<typename T>
struct EXPORT LockFactory {
    T* create();
    void destroy(T* p);
};

template<typename T>
struct EXPORT RecordingLockFactory {
    T* create();
    void destroy(T* p);
};

// ======== LockFactoryMgr ========
struct EXPORT LockFactoryMgr : public ILockFactoryMgr {
    LockFactoryMgr();
    ~LockFactoryMgr();
    static LockFactoryMgr& get();
    void init(FactoryType, const char* = nullptr, LockUseModelEnum = cDefaultModel, const char* = nullptr);
    void init(FactoryType);
    bool validate();
    int getFactoryType() const;
    void enableLocking(unsigned int, int);
    void enableLockingForResType(OpenAccess_4::oaLockResourceTypeEnum, int);
    bool isLockingEnabled() const;
    bool isLockingEnabled(unsigned int) const;
    bool isLockingEnabledForResType(OpenAccess_4::oaLockResourceTypeEnum) const;
    bool isLockingStateEnabled() const;
    void setLockingStateEnabled(int);
    void calcVMSize() const;
    void startRecording(const char*);
    void startRecording(const char*, unsigned int);
    void stopRecording();
    void stopRecording(unsigned int);
    void recordLockRelease(unsigned int, int);
    void recordLockRelease(int);
    unsigned int getThreadID();
    void registerThread(unsigned long);
    void unregisterThread(unsigned int);
    void registerUsage(const char*, LockUseModelEnum);
    void unregisterUsage(unsigned int);
    void* getLockFactory(const oaCommon::Guid&) const;
    void* getFactory();
    void* getUseModelResourceTypesMap();
    void* getResourceTypeVector(LockUseModelEnum);
    bool isResTypeIncludedInUseModel(LockUseModelEnum, OpenAccess_4::oaLockResourceTypeEnum) const;
    void* getThreadRecording(OpenAccess_4::oaLockResourceTypeEnum) const;
    long queryInterface(const oaCommon::Guid&, void**);
};

// ======== Implementation ========


MutexBase::MutexBase() : mtx_(nullptr), enabled_(true) { enabled_ = true; }
MutexBase::MutexBase(int) : mtx_(new mtuMutex()), enabled_(true) { enabled_ = true; }
MutexBase::~MutexBase() { delete mtx_; }

Mutex::Mutex(mtuMutex* m) { if(m) mtx_=m; else mtx_=new mtuMutex(); }
Mutex::~Mutex() {}
void Mutex::lock() { if(mtx_) pthread_mutex_lock(&mtx_->m); }
void Mutex::unlock() { if(mtx_) pthread_mutex_unlock(&mtx_->m); }

SpinMutex::SpinMutex(mtuSpinMutex* s) : smtx_(s?s:new mtuSpinMutex()), enabled_(true) { enabled_ = true; }
SpinMutex::~SpinMutex() { delete smtx_; }
void SpinMutex::lock() { if(smtx_) pthread_spin_lock(&smtx_->s); }
void SpinMutex::unlock() { if(smtx_) pthread_spin_unlock(&smtx_->s); }

RWLock::RWLock(mtuReadWriteLock* rw) : rwtx_(rw?rw:new mtuReadWriteLock()), enabled_(true) { enabled_ = true; }
RWLock::~RWLock() { delete rwtx_; }
void RWLock::lock(OpenAccess_4::oaRWLock::oaRWLockTypeEnum t) {
    if(rwtx_) { if(t==OpenAccess_4::oaRWLock::oacWriteLock) pthread_rwlock_wrlock(&rwtx_->rw); else pthread_rwlock_rdlock(&rwtx_->rw); }
}
void RWLock::unlock(OpenAccess_4::oaRWLock::oaRWLockTypeEnum) { if(rwtx_) pthread_rwlock_unlock(&rwtx_->rw); }

RecordingBase::RecordingBase() {}
RecordingBase::~RecordingBase() {}
RecordingBaseMutex::RecordingBaseMutex() : id_(0), resType_(OpenAccess_4::oacUnspecifiedResType) {}
RecordingBaseMutex::RecordingBaseMutex(unsigned int id, OpenAccess_4::oaLockResourceTypeEnum rt) : id_(id), resType_(rt) {}
RecordingBaseMutex::~RecordingBaseMutex() {}

RecordingMutex::RecordingMutex() : Mutex(nullptr), RecordingBaseMutex() {}
RecordingMutex::RecordingMutex(mtuMutex* m, unsigned int id, OpenAccess_4::oaLockResourceTypeEnum rt) : Mutex(m), RecordingBaseMutex(id,rt) {}
RecordingMutex::~RecordingMutex() {}
RecordingSpinMutex::RecordingSpinMutex() : SpinMutex(nullptr), RecordingBaseMutex() {}
RecordingSpinMutex::RecordingSpinMutex(mtuSpinMutex* s, unsigned int id, OpenAccess_4::oaLockResourceTypeEnum rt) : SpinMutex(s), RecordingBaseMutex(id,rt) {}
RecordingSpinMutex::~RecordingSpinMutex() {}
RecordingRWLock::RecordingRWLock() : RWLock(nullptr), RecordingBaseMutex() {}
RecordingRWLock::RecordingRWLock(mtuReadWriteLock* rw, unsigned int id, OpenAccess_4::oaLockResourceTypeEnum rt) : RWLock(rw), RecordingBaseMutex(id,rt) {}
RecordingRWLock::~RecordingRWLock() {}

ThreadRecording::ThreadRecording(const char*) {}
ThreadRecording::~ThreadRecording() {}
void ThreadRecording::reset(const char*) {}
bool ThreadRecording::isRecordingReady() const { return false; }
void ThreadRecording::record(ThreadRecord*) {}
void ThreadRecording::setRecordRelease(int) {}
int ThreadRecording::getRecordRelease() { return 0; }

template<typename T> T* LockFactory<T>::create() { return new T(); }
template<typename T> void LockFactory<T>::destroy(T* p) { delete p; }
template<typename T> T* RecordingLockFactory<T>::create() { return new T(); }
template<typename T> void RecordingLockFactory<T>::destroy(T* p) { delete p; }

LockFactoryMgr::LockFactoryMgr() {}
LockFactoryMgr::~LockFactoryMgr() {}
LockFactoryMgr& LockFactoryMgr::get() { static LockFactoryMgr m; return m; }
void LockFactoryMgr::init(FactoryType, const char*, LockUseModelEnum, const char*) {}
void LockFactoryMgr::init(FactoryType) {}
bool LockFactoryMgr::validate() { return true; }
int LockFactoryMgr::getFactoryType() const { return cDefaultFactory; }
void LockFactoryMgr::enableLocking(unsigned int, int) {}
void LockFactoryMgr::enableLockingForResType(OpenAccess_4::oaLockResourceTypeEnum, int) {}
bool LockFactoryMgr::isLockingEnabled() const { return true; }
bool LockFactoryMgr::isLockingEnabled(unsigned int) const { return true; }
bool LockFactoryMgr::isLockingEnabledForResType(OpenAccess_4::oaLockResourceTypeEnum) const { return true; }
bool LockFactoryMgr::isLockingStateEnabled() const { return true; }
void LockFactoryMgr::setLockingStateEnabled(int) {}
void LockFactoryMgr::calcVMSize() const {}
void LockFactoryMgr::startRecording(const char*) {}
void LockFactoryMgr::startRecording(const char*, unsigned int) {}
void LockFactoryMgr::stopRecording() {}
void LockFactoryMgr::stopRecording(unsigned int) {}
void LockFactoryMgr::recordLockRelease(unsigned int, int) {}
void LockFactoryMgr::recordLockRelease(int) {}
unsigned int LockFactoryMgr::getThreadID() { return 0; }
void LockFactoryMgr::registerThread(unsigned long) {}
void LockFactoryMgr::unregisterThread(unsigned int) {}
void LockFactoryMgr::registerUsage(const char*, LockUseModelEnum) {}
void LockFactoryMgr::unregisterUsage(unsigned int) {}
void* LockFactoryMgr::getLockFactory(const oaCommon::Guid&) const { return nullptr; }
void* LockFactoryMgr::getFactory() { return nullptr; }
void* LockFactoryMgr::getUseModelResourceTypesMap() { return nullptr; }
void* LockFactoryMgr::getResourceTypeVector(LockUseModelEnum) { return nullptr; }
bool LockFactoryMgr::isResTypeIncludedInUseModel(LockUseModelEnum, OpenAccess_4::oaLockResourceTypeEnum) const { return true; }
void* LockFactoryMgr::getThreadRecording(OpenAccess_4::oaLockResourceTypeEnum) const { return nullptr; }
long LockFactoryMgr::queryInterface(const oaCommon::Guid&, void**) { return 0; }

// Explicit template instantiations
template struct EXPORT LockFactory<Mutex>;
template struct EXPORT LockFactory<SpinMutex>;
template struct EXPORT LockFactory<RWLock>;
template struct EXPORT RecordingLockFactory<RecordingMutex>;
template struct EXPORT RecordingLockFactory<RecordingSpinMutex>;
template struct EXPORT RecordingLockFactory<RecordingRWLock>;

} // namespace AiviLock

extern "C" EXPORT long getClassObject(const char*, const oaCommon::Guid&, void** i) {
    *i = &AiviLock::LockFactoryMgr::get();
    return 0;
}
