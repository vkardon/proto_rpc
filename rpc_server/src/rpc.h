//
//  rpc.hpp
//
#ifndef __RPC_H__
#define __RPC_H__

//
// Class CRpc
//

// Solaris-specific support
// http://h21007.www2.hp.com/portal/download/files/unprot/STK/Solaris_STK/impacts/i255.html
//
#if defined(sun) || defined(__sun)
  // Define PORTMAP for backward compatibility includes:
  //#include <rpc/clnt_soc.h>
  //#include <rpc/svc_soc.h>
  #include <netdir.h>
  #define PORTMAP 1
#endif // SOLARIS

#include <rpc/rpc.h>
#include <string>

// Forward declaraiton for google::protobuf::Message
namespace google { namespace protobuf { class Message; } }

//
// Class CRpc
//
class CRpc
{
public:
    struct param
    {
        int type = 0;
        u_int data_len = 0;
        u_char* data_val = nullptr;
    };

    CRpc() = default;
    virtual ~CRpc() = default;

protected:
    static bool_t XdrParam(XDR* xdrs, param* pr, unsigned int)
    {
        if(!xdr_int(xdrs, &pr->type))
            return (FALSE);
        if(!xdr_array(xdrs, (char**)&pr->data_val, (u_int*)&pr->data_len, ~0, sizeof(u_char), (xdrproc_t)xdr_u_char))
            return (FALSE);
        return (TRUE);
    }

    // Protocol Buffers support
    int MsgToPtr(const google::protobuf::Message* msg, void** pptr);
    bool PtrToMsg(google::protobuf::Message* msg, const void* ptr, int size);
    void MsgPtrDelete(void* ptr) { if(ptr != nullptr) delete [] (unsigned char*)ptr; }
    void* MsgPtrClone(void* ptr, size_t size);

    // Logging support
    void LogInfo(const std::string& msg) { LogInfo(msg.c_str()); }
    void LogError(const std::string& err) { LogError(err.c_str()); }

    virtual void LogInfo(const char* msg) = 0;
    virtual void LogError(const char* err) = 0;
};

// One year timeout to simulate blocked call
#if defined(sun) || defined(__sun) // Solaris-specific support
    // Note: somehow one year timeout doesn't work on Solaris .
    // But two years timeout does work!
    const struct timeval RPC_TIMEOUT_INFINITE = {63072000,0};
#else
    // One year timeout
    const struct timeval RPC_TIMEOUT_INFINITE = {31536000,0};
#endif

//
// Class CRpcClient
//
class CRpcClient : public CRpc
{
public:
    CRpcClient() = default;
    virtual ~CRpcClient() { Destroy(); }
    
    bool Connect(const char* hostName, unsigned short port);
    bool IsValid() { return (cl != nullptr); }
    
    clnt_stat Call(int type, const google::protobuf::Message* req, google::protobuf::Message* resp,
                   const struct timeval timeout = RPC_TIMEOUT_INFINITE);
    
    clnt_stat Call(int type, const void* req, size_t reqSize, void*& resp, size_t& respSize,
                   const struct timeval timeout = RPC_TIMEOUT_INFINITE);

private:
    CLIENT* cl = nullptr;
    
    void Destroy();
};


//
// Class CRpcServer
//
class CRpcServer : public CRpc
{
public:
    CRpcServer();
    virtual ~CRpcServer() = default;
    
    // The maxPendingConnections defines the maximum length to which 
    // the queue of pending connections for the listening sockfd may grow.  
    // If a connection request arrives when the queue is full, the client 
    // may receive "connection refused" error.
    bool Run(unsigned short port, time_t timeoutSeconds, int maxPendingConnections=100);
    void Stop();
    
private:
    bool mContinueRunning = true;
    time_t mTimeoutSeconds = 1; // One second default pselect timeout
    static CRpcServer* mServer;
    static void RpcDispatch(struct svc_req* rqstp, SVCXPRT* transp);
    
    int CreateSocket(unsigned short port);
    int AcceptConnection(int sock);
    
protected:
    void HandleConnection(int fd);
    void GetClientInfo(int sock, std::string& clientName, std::string& clientIp);

    // Notification sent to derived class
    enum class NOTIFY_TYPE : char
    {
        WAITING_FOR_CONNECTION=1,  // Sent when timed out waiting for client to connect
        WAITING_FOR_CALL           // Sent when timed out waiting for client to make a call
    };

    // Note: The derived class might reset sock to 0 to do its own processing.
    // For example, to process the connection in a different thread.
    virtual bool OnConnection(int& sock) { return true; }

    virtual void OnNotify(NOTIFY_TYPE /*type*/) { /**/ }
    virtual bool OnCall(const CRpc::param* in, CRpc::param* out) = 0;
    virtual void OnCleanup(CRpc::param* out) = 0;
};


#endif /* defined(__RPC_H__) */
