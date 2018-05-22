//
// RPC Protobuf client
//

#include <stdio.h>      // printf()
#include <string>       
#include <unistd.h>     // sleep
#include <sys/wait.h>   // wait
#include "rpc.h"
#include "rpc.pb.h"     // Google Protocol Buffers generated header
#include "stopWatch.h"  // CStopWatch


class RpcClient : public CRpcClient
{
public:
    RpcClient() = default; 
    virtual ~RpcClient() = default;
    void EnableLogInfo(bool enable) { logInfoEnabled = enable; }
    void EnableLogError(bool enable) { logErrorEnabled = enable; }

    bool TestEcho(int numRpcs)
    {
        protorpc::EchoRequest req;
        protorpc::EchoResponse resp;

        CStopWatch stopWatch("Elapsed time [" + std::to_string(numRpcs) + " calls]: ");

        for(int i = 0; i < numRpcs; ++i)
        {
            // Protobuf test
            req.set_msg("Client pid=" + std::to_string(getpid()) + ", call #" + std::to_string(i+1));
            
            //printf("%s: Call() #%d, req='%s'\n", __func__, i+1, req.msg().c_str());

            clnt_stat res = Call(protorpc::RPC_ECHO, &req, &resp);
            //sleep(1);

            if(res != RPC_SUCCESS)
            {
                printf("%s: Call() failed\n", __func__);
                //assert(false);
                return false;
            }

            // Read response
            //printf("%s: Call() #%d, resp='%s'\n", __func__, i+1, resp.msg().c_str());
            
            if(req.msg() != resp.msg())
            {
                printf("%s: Call() failed: response is different from request:\n", __func__);
                printf("%s: req  is '%s'\n", __func__, req.msg().c_str());
                printf("%s: resp is '%s'\n", __func__, resp.msg().c_str());
                return false;
            }

            // Clean up - delete request buffer
            req.Clear();
            resp.Clear();
        } 
         
        return true;
    }

    bool TestData(int numRpcs)
    {
        CStopWatch stopWatch("Elapsed time [" + std::to_string(numRpcs) + " calls]: ");

        for(int i = 0; i < numRpcs; ++i)
        {
            // Data buffer test
            char* req = strdup("Hello from RPC client!");
            void* resp = NULL;
            size_t respSize = 0;
            
            printf("%s: Call() req ='%s'\n", __func__, req);

            clnt_stat res = Call(protorpc::RPC_DATA, req, strlen(req), resp, respSize);

            free(req);

            if(res != RPC_SUCCESS)
            {
                printf("%s: Call() failed\n", __func__);
                //assert(false);
                return false;
            }
            else
            {
                std::string val((const char*)resp, respSize);
                printf("%s: Call() resp='%s'\n", __func__, val.c_str());
            }

            if(resp)
                free(resp); // Clean up...
        } // end of for
         
        return true;
    }

    bool TestPing()
    {
        // Send an empty Ping message
        void* resp = NULL;
        size_t respSize = 0;

        clnt_stat res = Call(protorpc::RPC_PING, nullptr, 0, resp, respSize);
        
        if(res != RPC_SUCCESS)
        {
            printf("%s: Call() failed\n", __func__);
            //assert(false);
            return false;
        }
        else if(resp != nullptr || respSize != 0)
        {
            // We expect an empty response
            printf("%s: Call() failed (resp must be empty): resp=%p, respSize=%lu\n", __func__, resp, respSize);
            
            if(resp)
                free(resp); // Clean up...
            return false;
        }
        
        printf("%s: Call() succeeded\n", __func__);
        return true;
    }

private:
    virtual void LogInfo(const char* msg)
    {
        if(logInfoEnabled)
            printf("[INFO]: %s\n", msg);
    }
    
    virtual void LogError(const char* err)
    {
        if(logErrorEnabled)
            printf("[ERROR]: %s\n", err);
    }
    
    bool logInfoEnabled = true;
    bool logErrorEnabled = true;
};

int main(int argc, char *argv[])
{
    // Set stdout and stoerr to "line buffered": On output, data is written when
    // a newline character is inserted into the stream or when the buffer is full
    // (or flushed), whatever happens first.
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

    const char* host = "localhost";
    //const char* host = "dellse8rh1.advent.com";
    //const char* host = "opthex2zb.advent.com";
    unsigned short port = 53900;

    if(argc > 1 && !strcmp(argv[1], "echo"))
    {   
        // For Echo test, simulate multiple clients running simultaneously
        // Since RPC client doesn't support multithreading, let's use multiprocessing.
        //
        //const int numClients = 100; // Number or RPC clients to simulate
        const int numClients = 10; // Number or RPC clients to simulate
        //const int numRpcs = 10000;  // Number of RPCs to send per client
        const int numRpcs = 50;  // Number of RPCs to send per client
        
        printf("Simulating %d RPC clients, sending %d rpcs each...\n", numClients, numRpcs);
        
        for(int i=0; i<numClients; i++)
        {
            // Fork a child
            if(fork() == 0)
            {
                // Child process. Run the test
                RpcClient client;
                client.EnableLogInfo(false);
                if(!client.Connect(host, port))
                    return 1;
                client.TestEcho(numRpcs);
                return 0;
            }
        }
        
        // Parent process. Waiting for children processes to complete...
        while(true)
        {
            pid_t childPid = wait(nullptr);
            if(childPid < 0 && errno == ECHILD)
                break; // All children are done
        }
        
        printf("Done\n");
    }
    else if(argc > 1 && !strcmp(argv[1], "data"))
    {
        RpcClient client;
        if(!client.Connect(host, port))
            return 1;
        const int numRpcs = 1; // Number of RPCs to send
        client.TestData(numRpcs);
    }  
    else
    {
        // Create RPC client
        RpcClient client;
        if(!client.Connect(host, port))
            return 1;
        client.TestPing();
    } 
    
    return 0;
}
