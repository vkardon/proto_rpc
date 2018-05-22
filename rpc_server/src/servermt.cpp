//
//  main.cpp
//
#include "rpc.h"
#include "rpc.pb.h" // Google Protocol Buffers generated header
#include <unistd.h>
#include <signal.h>  // sigaction
#include <thread>
#include "threadPool.h"

class RpcServerMt : public CRpcServer
{
    class CTpool : public CThreadPool
    {
    public:
        CTpool(RpcServerMt* server) : mServer(server) {}
        ~CTpool(){ Destroy(); }
    
        virtual void OnInitThread(int threadIndx) { printf("OnInitThread: indx=%d\n", threadIndx); }
        virtual void OnExitThread(int threadIndx) { printf("OnExitThread: indx=%d\n", threadIndx); }
    
        virtual void OnThreadProc(int threadIndx, void* request)
        {   
            int sock = (int)(int64_t)request;

            // Get the connection host name and ip
            std::string clientName;
            std::string clientIp; 
            mServer->GetClientInfo(sock, clientName, clientIp);

            printf("%s [Thread %d]: Incoming connection from %s (%s), sock=%d\n",
                __func__, threadIndx, clientIp.c_str(), clientName.c_str(), sock);

            mServer->HandleConnection(sock);
        }   

        RpcServerMt* mServer = nullptr;
    };

public:
    RpcServerMt(int threadCount) : mTPool(this)
    {
#if defined (__APPLE__) || defined(__MACH__)
        mTPool.Create(threadCount, "RpcServerThreadPool");
#else
        mTPool.Create(threadCount);
#endif
    }
    ~RpcServerMt() = default;

private:
    bool mIsChildProcess = false;
    CTpool mTPool;   
 
    virtual bool OnConnection(int& sock)
    {
        mTPool.PostRequest((void*)(int64_t)sock);

        // Reset sock to 0 to have CRpcServer skip handling this connection.
        // This is because we do our own processing (in a different thread).
        sock = 0;
        return true;
    }

    virtual void OnNotify(NOTIFY_TYPE type)
    {
        if(type == NOTIFY_TYPE::WAITING_FOR_CONNECTION)
        {
            //printf("%d: WAITING_FOR_CONNECTION\n", getpid());

            // If we are the child process processing RPC connection, then
            // we must exit right after processing. This is important
            // since only the parent RPC Server process can accept new connections.
            if(mIsChildProcess)
            {
                Stop();
            }
        }
        else if(type == NOTIFY_TYPE::WAITING_FOR_CALL)
        {
            //printf("%d: WAITING_FOR_CALL\n", getpid());

//            static int count = 0;
//            if(++count == 10)
//                Stop();
        }
        else
        {
            printf("%d: Unknown\n", getpid());
        }
    }

    virtual bool OnCall(const CRpc::param* in, CRpc::param* out)
    {
        switch(in->type)
        {
            // Raw data RPC
            case protorpc::RPC_DATA:
            {
                // Data buffer test
                std::string val((const char*)in->data_val, in->data_len);

                //printf("%s: Data call req: '%s'\n", __func__, val.c_str());

                // Generate response
                const char* resp = strdup("Hello from RPC server!");
                out->data_val = (unsigned char*)resp;
                out->data_len = strlen(resp);

                //printf("%s: Data call resp: '%s'\n", __func__, resp);

                // NOTE: We cannot delete response buffer here since it has to be returned
                // Make sure to delete in in OnCleanup
            }
            break;
                
            // Empty rpc, no data received/sent
            case protorpc::RPC_PING:
            {
                // Send the empty response to show that we are running
                out->data_len = 0;
                out->data_val = (u_char*)nullptr;
            }
            break;

            // Empty rpc, no data received/sent
            case protorpc::RPC_SHUTDOWN:
            {
                // Note: We will be using OnCleanup to shutdown the server

                // Send the empty response to show that we are running
                out->data_len = 0;
                out->data_val = (u_char*)nullptr;
            }
            break;

            // Protobuf rpc
            case protorpc::RPC_ECHO:
            {
                // Read request
                protorpc::EchoRequest req;
                if(!CRpc::PtrToMsg(&req, in->data_val, (int)in->data_len))
                {
                    printf("%s: PtrToMsg failed\n", __func__);
                    return false;
                }

                // Generate response - echo request message back
                protorpc::EchoResponse resp;
                resp.set_msg(req.msg());
                
                req.Clear(); // Release req memory

                void* ptr = NULL;
                out->data_len = CRpc::MsgToPtr(&resp, &ptr);
                if(out->data_len == 0)
                {
                    printf("%s: MsgToPtr failed\n", __func__);
                    return false;
                }
                out->data_val = (u_char*)ptr;

                // Add random delay
                //usleep(rand()%9 * 10000);

                // test
                //out->data_len = 0;
                //out->data_val = NULL;
                // test end

                // NOTE: We cannot delete response buffer here since it has to be returned
                // Make sure to delete in in OnCleanup
            }
            break;

            default:
                printf("%s: Unknown message type=%d\n", __func__, in->type);
                return false;
        }

        return true;
    }

    virtual void OnCleanup(CRpc::param* out)
    {
        // We can use OnCleanup to shutdown the server
        if(out->type == protorpc::RPC_SHUTDOWN)
        {
            Stop();
        }

        // Clean up the response...
        if(out == nullptr || out->data_val == nullptr)
            return;

        switch(out->type)
        {
            case protorpc::RPC_DATA:
                free(out->data_val);
                break;

            // Is this for Protobuf message call?
            case protorpc::RPC_ECHO:
                CRpc::MsgPtrDelete(out->data_val);
                break;

            default:
                printf("%s: Unknown message type=%d\n", __func__, out->type);
                break;
         }

         out->data_val = nullptr;
         out->data_len = 0;
    }

    virtual void LogInfo(const char* msg)  { printf("[INFO] %d: %s\n", getpid(), msg); }
    virtual void LogError(const char* err) { printf("[ERROR] %d: %s\n", getpid(), err); }
};

int main(int argc, char* argv[])
{
    // Set stdout and stoerr to "line buffered": On output, data is written when
    // a newline character is inserted into the stream or when the buffer is full
    // (or flushed), whatever happens first.
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

    //unsigned short port = 8000;
    unsigned short port = 53900;
    int threadCount = 30;  // Number of threads to run

    printf("%d: RPC server started on port %d with %d threads...\n", getpid(), port, threadCount);

    RpcServerMt server(threadCount);
    server.Run(port, 2); // 2 seconds timeout

    return 0;
}
