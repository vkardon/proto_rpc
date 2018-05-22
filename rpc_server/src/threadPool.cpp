/*
Commercial-in-confidence information of Mesh Networks Holdings IP Pty Ltd atf the Mesh Networks IP Unit Trust. 
Not to be disclosed without the written consent of Mesh Networks Holdings IP Pty Ltd.
Copyright 2013 - Mesh Networks Holdings IP Pty Ltd atf Mesh Networks IP Unit Trust
*/

//
//  ThreadPool.cpp
//
#include <memory.h>
//#include <stdio.h> // for printf
#include "threadPool.h"


CThreadPool::CThreadPool() : mThreadsIdArr(NULL), mThreadCount(0), mReady(false)
{
    //...
}

CThreadPool::~CThreadPool()
{
    // Note: Calling Destroy() from CThreadPool destructor will
    // call virtual OnExitThread, which is not a good idea.
    // If derived class overrides OnExitThread, then calling it
    // from the base class destructor might be unpredictable
    // since derived class is already destroyed.
    
    // Make sure to explicitly call Destroy() in your derived class
//    CMutexLock s(mMutex);
//    if(mReady)
//    {
//        // Warning: the pool is still running.
//        ASSERT(!mReady, "CThreadPool::Destroy should be called prior to destroying the pool");
//    }
}

#ifdef __APPLE__
bool CThreadPool::Create(int threadCount, const char* poolName /* used to identify semaphores */)
#else
bool CThreadPool::Create(int threadCount)
#endif // __APPLE__
{
    if(threadCount <= 0)
        return false;
    
    mThreadCount = threadCount;
    
    // Create semaphore to wait untill all threads are ready
    CSemaphore semaphoreReady;
#ifdef __APPLE__
    semaphoreReady.Create("thread_pool_thread_ready");
#else
    semaphoreReady.Create();
#endif // __APPLE__
    if(!semaphoreReady.IsValid())
        goto lError; // Failed to init semaphore
    
    // Create semaphore to manage pool of threads
#ifdef __APPLE__
    mSemaphore.Create(poolName);
#else
    mSemaphore.Create();
#endif // __APPLE__
    if(!mSemaphore.IsValid())
        goto lError; // Failed to init semaphore
    
    // Create mutex
    mMutex.Create();
    if(!mMutex.IsValid())
        goto lError; // Failed to create mutex
    
    // Create worked threads
    if((mThreadsIdArr = new pthread_t[mThreadCount]) == NULL)
        goto lError;
    memset(mThreadsIdArr, 0, sizeof(pthread_t) * mThreadCount);
    
    CThreadData* threadData;
    for(int i=0; i < mThreadCount; i++)
    {
        if((threadData = new CThreadData) == NULL)
            goto lError;
        threadData->mPool = this;
        threadData->mIndex = i;
        threadData->mSemaphoreReady = &semaphoreReady;
        
        if(pthread_create(&mThreadsIdArr[i], NULL, ThreadPoolProc, threadData) != 0)
            goto lError; // Failed to create thread
    }
    
    // Loop untill all threads are ready
    for(int i=0; i < mThreadCount; i++)
    {
        semaphoreReady.Wait();
        //printf("Thread %d of %d is initialized\n", i+1, mThreadCount);
    }
    
    //printf("All threads are initialized\n");
    
    // We are ready!
    {
        CMutexLock s(mMutex);
        mReady = true;
    }
    
    return true; // Success
    
    // Oops...something went wrong and we cannot continue.
lError:
    
    // Destroy threads, semaphore, mutex...
    if(mThreadsIdArr != NULL)
    {
        for(int i=0; i < mThreadCount && mThreadsIdArr[i] != 0; i++)
        {
            // Detatch thread first to don't join it
            pthread_detach(mThreadsIdArr[i]);
            pthread_cancel(mThreadsIdArr[i]);
        }
        delete [] mThreadsIdArr;
        mThreadsIdArr = NULL;
    }
    
    mSemaphore.Destroy();
    mMutex.Destroy();
    
    return false;
}

void CThreadPool::Destroy(bool waitToFinish)
{
    {
        CMutexLock s(mMutex);
        if(!mReady)
            return; // The pool has not been created yet or already destroyed
        
        mReady = false; // Prevent from queuing new requests
        
        // Note: Set hight priority flag to 'false' to wait
        // untill all current requests are processsed
        bool highPriority = !waitToFinish;
        
        // Post 'exit' request to all threads
        for(int i = 0; i < mThreadCount; i++)
        {
            if(highPriority)
                mRequestList.push_front(CRequest((void*)(-1)));
            else
                mRequestList.push_back(CRequest((void*)(-1)));
            
            // Indicate that another request is available
            mSemaphore.Post();
        }
    }
    
    //printf("Wait for %d threads to finish\n", mThreadCount);
    
    // Wait for all the threads to finish
    for(int i=0; i < mThreadCount; i++)
        pthread_join(mThreadsIdArr[i], NULL);
    
    //printf("All threads are finished\n");
    
    delete [] mThreadsIdArr;
    mThreadsIdArr = NULL;
    
    // Destroy semaphore and mutex...
    mSemaphore.Destroy();
    mMutex.Destroy();
}

bool CThreadPool::PostRequest(void* request, bool highPriority)
{
    if(request == NULL || request == (void*)(-1)) // (-1) will force thread to exit
        return false;
    
    // Add request to list
    {
        CMutexLock s(mMutex);
        if(!mReady)
            return false;
        if(highPriority)
            mRequestList.push_front(CRequest(request));
        else
            mRequestList.push_back(CRequest(request));
    }
    
    // Update semaphore counter
    return (mSemaphore.Post() == 0);
}

void* CThreadPool::ThreadPoolProc(void* param)
{
    if(param == NULL)
        return (void*)1;
    
    // Put the thread in cancelable state, so we can cancel it
    // if needed - for example, if Thread Pool creation failed
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    CThreadData* threadData = (CThreadData*)param;
    CSemaphore* semaphoreReady = threadData->mSemaphoreReady;
    CThreadPool* pool = threadData->mPool;
    int threadIndx = threadData->mIndex;
    delete threadData;
    
    pool->OnInitThread(threadIndx);
    
    // Singal that the thread are ready
    if(semaphoreReady != NULL)
        semaphoreReady->Post();
    
    while(true)
    {
        pool->mSemaphore.Wait();
        
        // Get request
        void* request = pool->GetNextRequest();
        if(request == (void*)(-1))
            break; // Exit the thread
        
        // Process request
        pool->OnThreadProc(threadIndx, request);
    }
    
    pool->OnExitThread(threadIndx);
    
    return (void*)0;
}

int CThreadPool::GetReqCount()
{
    CMutexLock s(mMutex);
    return (int)mRequestList.size();
}

void* CThreadPool::GetNextRequest()
{
    CMutexLock s(mMutex);
    std::list<CRequest>::iterator itr = mRequestList.begin();
    if(itr == mRequestList.end())
        return NULL;
    void* request = itr->mRequest;
    mRequestList.erase(itr);
    return request;
}

