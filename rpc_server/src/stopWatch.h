//
// stopWatch.h
//
#ifndef __STOP_WATCH_H__
#define __STOP_WATCH_H__

#include <stdio.h>      // printf()
#include <string>
#include <sys/time.h>

class CStopWatch
{
    struct timeval start_tv;
    struct timeval stop_tv;
    std::string prefix;
    bool silentOnExit = false;

public:
    CStopWatch(const char* _prefix="", bool _silentOnExit=false) 
        : prefix(_prefix) , silentOnExit(_silentOnExit) 
    { 
        Start(); 
    }

    CStopWatch(const std::string& _prefix, bool _silentOnExit=false) 
        : CStopWatch(_prefix.c_str(), _silentOnExit) {}

    ~CStopWatch() 
    {  
        if(!silentOnExit) 
           Stop();  
    }

    void Start() 
    { 
        gettimeofday(&start_tv, NULL); 
    }

    void Stop()
    {
        gettimeofday(&stop_tv, NULL);
        //long long elapsed = (stop_tv.tv_sec - start_tv.tv_sec)*1000000 + (stop_tv.tv_usec - start_tv.tv_usec);
        //printf("%lld microseconds\n", elapsed);

        timeval tmp;
        if(stop_tv.tv_usec < start_tv.tv_usec)
        {
            tmp.tv_sec = stop_tv.tv_sec - start_tv.tv_sec - 1;
            tmp.tv_usec = 1000000 + stop_tv.tv_usec - start_tv.tv_usec;
        }
        else
        {
            tmp.tv_sec = stop_tv.tv_sec - start_tv.tv_sec;
            tmp.tv_usec = stop_tv.tv_usec - start_tv.tv_usec;
        }

        printf("%s%ld.%06lu sec\n", prefix.c_str(), tmp.tv_sec, (long)tmp.tv_usec);
        fflush(stdout);
    }
};

#endif // __STOP_WATCH_H__
