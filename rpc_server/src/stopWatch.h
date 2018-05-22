//
// stopWatch.h
//
#ifndef __TIME_ELAPSED_H__
#define __TIME_ELAPSED_H__

#include <stdio.h>      // printf()
#include <string>
#include <sys/time.h>

class CStopWatch
{
    struct timeval start_tv;
    struct timeval stop_tv;
    std::string prefix;

public:
    CStopWatch(const char* _prefix="") : prefix(_prefix) { gettimeofday(&start_tv, NULL); }
    CStopWatch(const std::string _prefix="") : prefix(_prefix) { gettimeofday(&start_tv, NULL); }
    ~CStopWatch()
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

#endif // __TIME_ELAPSED_H__
