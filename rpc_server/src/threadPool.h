/*
Commercial-in-confidence information of Mesh Networks Holdings IP Pty Ltd atf the Mesh Networks IP Unit Trust. 
Not to be disclosed without the written consent of Mesh Networks Holdings IP Pty Ltd.
Copyright 2013 - Mesh Networks Holdings IP Pty Ltd atf Mesh Networks IP Unit Trust
*/

//
//  ThreadPool
//
#ifndef __HunninMesh__ThreadPool__
#define __HunninMesh__ThreadPool__

#include <pthread.h>
#include <semaphore.h>
#include <list>

//
// Class CSemaphore
//
class CSemaphore
{
public:
#ifdef __APPLE__
    CSemaphore(const char* semName=NULL) : mIsValid(false), mSem(SEM_FAILED)
    {
        if(semName)
            Create(semName);
    }
#else
    CSemaphore(bool create=false) : mIsValid(false), mSem()
    {
        if(create)
            Create();
    }
#endif // __APPLE__
    
    ~CSemaphore()
    {
        Destroy();
    }
    
#ifdef __APPLE__
    int Create(const char* semName)
    {
        if(mIsValid)
            return -1; // Do not recreate
        // Note: total semaphore name should not exceeded SEM_NAME_LEN characters (30)
        sem_unlink(semName);
        mSem = sem_open(semName,
                        O_CREAT | O_EXCL, /* create if does not exist, error if exists */
                        S_IRUSR | S_IWUSR /* read-write for owner*/,
                        0 /* initial count */);
        mIsValid = (mSem != SEM_FAILED);
        return (mIsValid ? 0 : -1);
    }
#else
    int Create()
    {
        if(mIsValid)
            return -1; // Do not recreate
        int res = sem_init(&mSem, 0, 0);
        mIsValid = (res == 0);
        return res;
    }
#endif // __APPLE__
    
    int Destroy()
    {
        if(!mIsValid)
            return -1; // Invalid semaphore descriptor
        mIsValid = false;
        
#ifdef __APPLE__
        int res = sem_close(mSem);
        mSem = SEM_FAILED;
#else
        int res = sem_destroy(&mSem);
        mSem = sem_t();
#endif // __APPLE__
        return res;
    }
    
    int Post()
    {
        if(!mIsValid)
            return -1; // Invalid semaphore descriptor
#ifdef __APPLE__
        return sem_post(mSem);
#else
        return sem_post(&mSem);
#endif // __APPLE__
    }
    
    int Wait()
    {
        if(!mIsValid)
            return -1; // Invalid semaphore descriptor
#ifdef __APPLE__
        return sem_wait(mSem);
#else
        return sem_wait(&mSem);
#endif // __APPLE__
    }
    
    bool IsValid() { return mIsValid; }
    
private:
    bool mIsValid;
    
#ifdef __APPLE__
    sem_t* mSem;
#else
    sem_t mSem;
#endif // __APPLE__
};

//
// Class CMutex
//
class CMutex
{
public:
    CMutex(bool create=false) : mIsValid(false)
    {
        if(create)
            Create();
    }
    
    ~CMutex()
    {
        Destroy();
    }
    
    int Create()
    {
        if(mIsValid)
            return -1; // Do not recreate
        int res = pthread_mutex_init(&mMutex, NULL);
        mIsValid = (res == 0);
        return res;
    }
    
    int Destroy()
    {
        if(!mIsValid)
            return -1;
        mIsValid = false;
        return pthread_mutex_destroy(&mMutex);
    }
    
    int Lock()     { return ( mIsValid ? pthread_mutex_lock(&mMutex) : -1); }
    int Unlock()   { return ( mIsValid ? pthread_mutex_unlock(&mMutex) : -1); }
    bool IsValid() { return mIsValid; }
    
private:
    pthread_mutex_t mMutex;
    bool mIsValid;
};

//
// Helper class CMutexLock
//
class CMutexLock
{
public:
    CMutexLock(CMutex& mutex) : mMutex(mutex) { mMutex.Lock(); }
    ~CMutexLock() { mMutex.Unlock(); }
private:
    CMutex& mMutex;
};

//
// Abstract class CThreadPool
//
class CThreadPool
{
    // Helper structure CRequest
    struct CRequest
    {
        CRequest(void* request) : mRequest(request) {}
        void* mRequest;
    };
    
    // Helper structure CThreadData
    struct CThreadData
    {
        CThreadPool* mPool;
        CSemaphore* mSemaphoreReady;
        int mIndex;
    };
    
    // Contraction/destruction
public:
    CThreadPool();
    virtual ~CThreadPool();
    
    // Class data
private:
    CMutex     mMutex;
    CSemaphore mSemaphore;
    pthread_t* mThreadsIdArr;
    int        mThreadCount;
    bool       mReady;
    std::list<CRequest> mRequestList;
    
    // Implementation
private:
    void* GetNextRequest();
    
    // Thread's procedure
    static void* ThreadPoolProc(void*);
    
public:
#ifdef __APPLE__
    bool Create(int threadCount, const char* poolName /* used to identify semaphores */);
#else
    bool Create(int threadCount);
#endif // __APPLE__
    
    void Destroy(bool waitToFinish = false);
    bool PostRequest(void* request, bool highPriority = false);
    int  GetReqCount();
    
    // Overrides
protected:
    virtual void OnInitThread(int threadIndx) {};
    virtual void OnExitThread(int threadIndx) {};
    virtual void OnThreadProc(int threadIndx, void* request) = 0;
};

#endif /* defined(__HunninMesh__ThreadPool__) */

