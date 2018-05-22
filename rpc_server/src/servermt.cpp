//
//  main.cpp
//
#include "rpc.h"
#include "rpc.pb.h" // Google Protocol Buffers generated header
#include <unistd.h>
#include <signal.h>  // sigaction
#include <thread>

extern "C" typedef void (*fSigHandler)(int);  

int Signal(int signum, fSigHandler handler, fSigHandler* ohandler = NULL)
{
    struct sigaction sa, old_sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Restart interrupted system calls 

    int ret = sigaction(signum, &sa, &old_sa);
    if(ohandler)
        *ohandler = old_sa.sa_handler;

    return ret;
}

class RpcServerMt : public CRpcServer
{
public:
    RpcServerMt() = default;
    ~RpcServerMt() = default;

private:
    bool mIsChildProcess = false;
    std::thread threads[128]{};

    virtual bool OnConnection(int& sock)
    {
        // Get the connection host name and ip
        std::string clientName;
        std::string clientIp;
        GetClientInfo(sock, clientName, clientIp);

        //printf("%s: Incoming connection from %s (%s), sock=%d\n",
        //        __func__, clientIp.c_str(), clientName.c_str(), sock);

        // Check if the socket withing the bound of our thread array
        if(sock >= (sizeof(threads) / sizeof(std::thread)))
        {
            printf("%s ERROR: The sock=%d is out of bound\n", __func__, sock);
            return false;
        }

        if(threads[sock].joinable())
        {
            printf("%s ERROR: The thread for sock=%d already running\n", __func__, sock);
            return false;
        }

        // Start new thread to handle the connection
        threads[sock] = std::thread([&, sock]()
        {
            printf("Processing new connection: sock=%d, thread=%lld\n", sock, (long long)pthread_self());

            HandleConnection(sock);
            threads[sock].detach(); // We are done with the thread
        });

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

    fSigHandler old_SIGCHLD_handler = NULL;

    // Ignore the SIGCHLD to prevent children from transforming into
    // zombies so we don't need to wait and reap them.
    if(Signal(SIGCHLD, SIG_IGN, &old_SIGCHLD_handler) < 0)
    {
        printf("ERROR: sigaction(SIGCHLD) failed: %s\n", strerror(errno));
        return 1;
    }

    printf("%d: RPC server started on port %d ...\n", getpid(), port);

    RpcServerMt server;
    server.Run(port, 2); // 2 seconds timeout

    //printf("%d: RPC server: stopped\n", getpid());

    // Restore the original SIGCHLD handler
    if(Signal(SIGCHLD, old_SIGCHLD_handler) < 0)
    {
        printf("ERROR: sigaction(SIGCHLD old) failed: %s\n", strerror(errno));
    }

    return 0; // NOTREACHED
}
