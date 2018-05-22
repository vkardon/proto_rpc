//
//  rpc.cpp
//

//
// Note: For Solaris build: http://docs.oracle.com/cd/E36784_01/html/E36875/svctcp-create-3nsl.html
// Routines as clnttcp_create(), svctcp_create, and some others take a pointer to 
// a file descriptor that is a socket. In this case the application will have to be 
// linked with both librpcsoc and libnsl. 
//
// Make sure to set LD_LIBRARY_PATH accordigly:
//
// /usr/ucblib/librpcsoc.so.1		// 32-bit shared object
// /usr/ucblib/64/librpcsoc.so.1	// 64-bit shared object
// /usr/ucblib/amd64/librpcsoc.so.1	// 64-bit shared object
//

#include <stdio.h>
#include <memory.h>
#include <netdb.h>      // gethostbyname
#include <unistd.h>     // sleep
#include <assert.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sstream>
#include <google/protobuf/message.h>
#include "rpc.h"

#ifndef __DISPATCH_FN_T // Defined on Linux, not defined in OSX
#define __DISPATCH_FN_T
  #if defined (__APPLE__) || defined(__MACH__)
    extern "C" typedef void (*__dispatch_fn_t)();
  #else
    extern "C" typedef void (*__dispatch_fn_t)(struct svc_req*, SVCXPRT*);
  #endif
#endif

#define RPC_PROTOBUF_PROG_NUMBER        ((u_int)0x2fffffff)
#define RPC_PROTOBUF_VERSION            ((u_int)1)
#define RPC_PROTOBUF_FUNC_PROC          ((u_int)1) // function to call

// Logging helpers
#define INFOMSG(className, msg)                          \
do{                                                      \
    std::stringstream ss;                                \
    ss << className << "::" << __func__ << ": " << msg;  \
    LogInfo(ss.str().c_str());                           \
}while(0)    

#define ERRMSG(className, msg)                           \
do{                                                      \
    std::stringstream ss;                                \
    ss << className << "::" << __func__ << ": " << msg;  \
    LogError(ss.str().c_str());                          \
}while(0)    


//
// Class CRpc
//
int CRpc::MsgToPtr(const google::protobuf::Message* msg, void** pptr)
{
    assert(msg != nullptr);
    *pptr = nullptr; // Initially

    int size = msg->ByteSize();
    if(size == 0)
    {
        ERRMSG("CRpc", "Uninitialized or invalid protobuf message: size=" << size);
        assert(false);
        return 0;
    }

    *pptr = new (std::nothrow) unsigned char[size];
    if(*pptr == nullptr)
    {
        ERRMSG("CRpc", "Failed to allocate protobuf message of " << size << " bytes");
        assert(false);
        return 0;
    }

    if(!msg->SerializeToArray(*pptr, size))
    {
        delete [] (unsigned char*)*pptr;
        *pptr = nullptr;
        ERRMSG("CRpc", "Failed to write protobuf message: size=" << size);
        assert(false);
        return 0;
    }

    return size;
}

bool CRpc::PtrToMsg(google::protobuf::Message* msg, const void* ptr, int size)
{
    assert(msg != nullptr);
    if(!msg->ParseFromArray(ptr, size))
    {
        msg->Clear();
        ERRMSG("CRpc", "Failed to read protobuf message: size=" << size);
        assert(false);
        return false;
    }

    return true;
}

void* CRpc::MsgPtrClone(void* ptr, size_t size)
{
    unsigned char* new_ptr = new (std::nothrow) unsigned char[size];
    if(new_ptr == nullptr)
        return nullptr;
    memcpy(new_ptr, ptr, size);
    return new_ptr;
}


//
// Class CRpcClient
//
bool CRpcClient::Connect(const char* hostName, unsigned short port)
{
    if(hostName == nullptr)
    {
        ERRMSG("CRpcClient", "Invalid (an empty) hostName specified");
        return false;
    }
    
    if(port == 0)
    {
        ERRMSG("CRpcClient", "Invalid (zero) port number specified");
        return false;
    }

    if(cl != nullptr)
    {
        ERRMSG("CRpcClient", "Failed for " << hostName << ":" << port << " - the CLIENT already exists");
        return false;
    }

    INFOMSG("CRpcClient", "Connecting to " << hostName << ":" << port);
    
    // Connent to the server located at Internet address *addr.
    struct hostent* h = gethostbyname(hostName);
    if(h == nullptr)
    {
        ERRMSG("CRpcClient", "gethostbyname(" << hostName << ") failed: " << hstrerror(h_errno));
        return false;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, h->h_addr_list[0], sizeof(in_addr_t));

    int sock = RPC_ANYSOCK; // To open new socket

    // Create RPC client for the remote program on the designated hostname/port.
    cl = clnttcp_create(&addr,
                        RPC_PROTOBUF_PROG_NUMBER,   // program number
                        RPC_PROTOBUF_VERSION,       // version number
                        &sock,                      // socket to be set
                        0,                          // send_buf_size
                        0);                         // recv_buf_size

    //printf("CRpcClient: clnttcp_create() socket=%d\n", sock);

    if(cl == nullptr)
    {
        ERRMSG("CRpcClient", "clnttcp_create(" << hostName << ":" << port << ") failed" << clnt_spcreateerror((char*)""));
        return false;
    }
    
    INFOMSG("CRpcClient", "Succeeded");
    return true;
}

void CRpcClient::Destroy()
{
    // Destroys the client's RPC handle by deallocating all memory related to the handle.
    if(cl != nullptr)
    {
        //INFOMSG("CRpcClient", "");
        clnt_destroy(cl);
        cl = nullptr;
    }
}

clnt_stat CRpcClient::Call(int type, const google::protobuf::Message* req, google::protobuf::Message* resp,
               const struct timeval timeout)
{
    if(cl == nullptr)
    {
        ERRMSG("CRpcClient", "Client is not yet connected");
        return RPC_FAILED;
    }

    void* ptr = nullptr;
    int len = 0;

    if(req != nullptr)
    {
        len = CRpc::MsgToPtr(req, &ptr);
        if(len == 0)
        {
            ERRMSG("CRpcClient", "MsgToPtr failed");
            return RPC_FAILED;
        }
    }

    param in;
    in.type = type;
    in.data_val = (u_char*)ptr;
    in.data_len = (u_int)len;

    param out;
    //memset((char*)&out, 0, sizeof(out)); // initialized in defaut constructor instead

    clnt_stat res = clnt_call(cl, RPC_PROTOBUF_FUNC_PROC,
                              (xdrproc_t)CRpc::XdrParam, (caddr_t)&in,
                              (xdrproc_t)CRpc::XdrParam, (caddr_t)&out, timeout);

    if(res != RPC_SUCCESS)
    {
        ERRMSG("CRpcClient", "clnt_call() failed" << clnt_sperror(cl, (char*)""));
    }
    // Is response expected?
    else if(resp != nullptr)
    {
        if(out.data_len == 0 || out.data_val == nullptr)
        {
            // If response is expected, but not recieved then something went wrong
            ERRMSG("CRpcClient", "No response received "
                << "(data_len=" << out.data_len << ", data_val=" << (out.data_val == nullptr ? "nullptr" : "NOT nullptr") << ")");
            res = RPC_FAILED;
        }
        else if(!CRpc::PtrToMsg(resp, out.data_val, (int)out.data_len))
        {
            ERRMSG("CRpcClient", "PtrToMsg failed");
            res = RPC_FAILED;
        }
    }
    // We don't expect any response
    else
    {
        if(out.data_len != 0 || out.data_val != nullptr)
        {
            // If response is NOT expected, but recieved then something went wrong
            ERRMSG("CRpcClient", "Unexpected response received "
                << "(data_len=" << out.data_len << ", data_val=" << (out.data_val == nullptr ? "nullptr" : "NOT nullptr") << ")");
            res = RPC_FAILED;
        }
    }

    // Free the memory that was allocated when RPC result was decoded
    if(!clnt_freeres(cl, (xdrproc_t)CRpc::XdrParam, (caddr_t)&out))
    {
        ERRMSG("CRpcClient", "clnt_freeres() failed" << clnt_sperror(cl, (char*)""));
    }

    // Clean up - delete request buffer
    CRpc::MsgPtrDelete(ptr);
    ptr = nullptr;

    // If RPC failed due to failure to send or receive, then destroy the client and reconnect
    if(res == RPC_CANTSEND || res == RPC_CANTRECV)
        Destroy();

    return res;
}

clnt_stat CRpcClient::Call(int type, const void* req, size_t reqSize, void*& resp, size_t& respSize,
               const struct timeval timeout /*= RPC_TIMEOUT_INFINITE*/)
{
    if(cl == nullptr)
    {
        ERRMSG("CRpcClient", "Client is not yet connected");
        return RPC_FAILED;
    }

    resp = nullptr;  // initially
    respSize = 0; // initially

    param in;
    in.type = type;
    in.data_val = (u_char*)req;
    in.data_len = (u_int)reqSize;

    param out;
    //memset((char*)&out, 0, sizeof(out)); // initialized in defaut constructor instead

    clnt_stat res = clnt_call(cl, RPC_PROTOBUF_FUNC_PROC,
                              (xdrproc_t)CRpc::XdrParam, (caddr_t)&in,
                              (xdrproc_t)CRpc::XdrParam, (caddr_t)&out, timeout);

    if(res != RPC_SUCCESS)
    {
        ERRMSG("CRpcClient", "clnt_call() failed" << clnt_sperror(cl, (char*)""));
    }

    // Do we have any response?
    if((out.data_len == 0 && out.data_val != nullptr) ||
       (out.data_len != 0 && out.data_val == nullptr))
    {
        // Invalid response data - something went wrong
        ERRMSG("CRpcClient", "Invalid response received "
            << "(data_len=" << out.data_len << ", data_val=" << (out.data_val == nullptr ? "nullptr" : "NOT nullptr") << ")");
        res = RPC_FAILED;
    }
    else if(out.data_len == 0 && out.data_val == nullptr)
    {
        // Empty response
        resp = nullptr;
        respSize = 0;
    }
    else
    {
        resp = malloc(out.data_len);
        if(resp)
        {
            memcpy(resp, out.data_val, out.data_len);
            respSize = out.data_len;
        }
        else
        {
            ERRMSG("CRpcClient", "Failed to allocate " << out.data_len << " bytes");
            res = RPC_FAILED;
        }
    }

    // Free the memory that was allocated when RPC result was decoded
    if(!clnt_freeres(cl, (xdrproc_t)CRpc::XdrParam, (caddr_t)&out))
    {
        ERRMSG("CRpcClient", "clnt_freeres() failed" << clnt_sperror(cl, (char*)""));
    }

    // If RPC failed due to failure to send or receive, then destroy the client and reconnect
    if(res == RPC_CANTSEND || res == RPC_CANTRECV)
        Destroy();

    return res;
}


//
// Class CRpcServer
//
#include <rpc/pmap_clnt.h>
#include <unistd.h>         // close() 
#include <errno.h>

CRpcServer* CRpcServer::mServer = nullptr;

CRpcServer::CRpcServer()
{
    if(mServer != nullptr)
        return;
    
    // TODO: Make CRpcServer singleton
    mServer = this;
}

//bool CRpcServer::Run(unsigned short port)
//{
//    if(port == 0)
//    {
//        ERRMSG("CRpcServer", "Invalid (zero) port number specified");
//        return false;
//    }
//
//    // Create RPC service to listen on the designated port.
//    int sock = CreateSocket(port);
//    if(sock < 0)
//        return false;
//
//    // Create a TCP/IP-based RPC service transport and associate it with the socket.
//    SVCXPRT* transp = svctcp_create(sock, 0 /*send_buf_size*/, 0 /*recv_buf_size*/);
//    if(transp == nullptr)
//    {
//        std::stringstream msg;
//        ERRMSG("CRpcServer",
//            "svctcp_create(sock=" << sock << ") failed to create TCP service, strerror: " << strerror(errno));
//        close(sock);
//        return false;
//    }
////    else
////    {
////        INFOMSG("CRpcServer", "svctcp_create() succeeded: sock=" << transp->xp_sock << ", port=" << //transp->xp_port);
////    }
//
//    __dispatch_fn_t dispatch = (__dispatch_fn_t)CRpcServer::RpcDispatch;
//
//    // Associates prognum and versnum with the service dispatch procedure (dispatch),
//    // but DO NOT register with the portmap service.
//    if(!svc_register(transp, RPC_PROTOBUF_PROG_NUMBER, RPC_PROTOBUF_VERSION, dispatch, 0 /*do not register with portmapper*/))
//    {
//        ERRMSG("CRpcServer", "svc_register() failed");
//        svc_destroy(transp);
//        transp = nullptr;
//        close(sock);
//        return false;
//    }
//
//    INFOMSG("CRpcServer", "Entering svc_run()");
//
//    svc_run();
//
//    // Clean up...
//    if(transp != nullptr)
//        svc_destroy(transp);
//    transp = nullptr;
//
//    if(sock > 0)
//        close(sock);
//    sock = -1;
//
//    // TODO - not tested
//    svc_unregister(RPC_PROTOBUF_PROG_NUMBER, RPC_PROTOBUF_VERSION);
//    if(!pmap_unset(RPC_PROTOBUF_PROG_NUMBER, RPC_PROTOBUF_VERSION))
//        ERRMSG("CRpcServer", "pmap_unset() failed");
//
//    INFOMSG("CRpcServer", "svc_run() returned");
//    return true;
//}

bool CRpcServer::Run(unsigned short port, time_t timeoutSeconds, int maxPendingConnections /*=100*/)
{
    if(port == 0)
    {
        ERRMSG("CRpcServer", "Invalid (zero) port number specified");
        return false;
    }

    // Create RPC service to listen on the designated port.
    int sock = CreateSocket(port);
    if(sock < 0)
        return false;

    // Start to listen
    if(listen(sock, maxPendingConnections) != 0)
    {
        ERRMSG("CRpcServer", "listen() failed, sock=" << sock << ": " << strerror(errno));

        if(close(sock) != 0)
        {
            ERRMSG("CRpcServer", "close() failed, sock=" << sock << ": " << strerror(errno));
        }

        sock = -1;
        return false;
    }

    INFOMSG("CRpcServer", "Waiting for client to connect on port " << port);

    // Loop looking for connections / data to handle
    mTimeoutSeconds = timeoutSeconds;
    struct timespec timeout = {mTimeoutSeconds,0};
    fd_set readfds;
    int res = 0;

    while(mContinueRunning)
    {
        OnNotify(NOTIFY_TYPE::WAITING_FOR_CONNECTION);
        if(!mContinueRunning)
            break;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        res = pselect(sock+1, &readfds, nullptr, nullptr, &timeout, nullptr);

        if(res < 0)
        {
            if(errno == EINTR)
            {
                INFOMSG("CRpcServer", "pselect() interrupted with EINTR signal, continue running");
            }
            else
            {
                ERRMSG("CRpcServer", "pselect() failed, sock=" << sock << ": " << strerror(errno));
                break;
            }
        }
        else if(res == 0)
        {
//            INFOMSG("CRpcServer", "pselect() timed out in " << mTimeoutSeconds << " seconds, continue running");
        }
        else
        {
            // Accept incoming client connection.
            int sockCon = AcceptConnection(sock);
            if(sockCon > 0)
            {
                // Process new client connection.
                // If will not return until the connection closed/lost.
                HandleConnection(sockCon);
            }
        }
    }

    // Clean up...
    if(sock > 0)
        close(sock);
    sock = -1;

    INFOMSG("CRpcServer", "Stopped");
    return true;
}

void CRpcServer::Stop()
{
    mContinueRunning = false;
    INFOMSG("CRpcServer", "Stopping RPC server...");
}

int CRpcServer::CreateSocket(unsigned short port)
{
    // Open up the TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock < 0)
    {
        ERRMSG("CRpcServer",  "socket() failed: " << strerror(errno));
        return -1;
    }

    // Bind the socket to all local addresses on the designated port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // bind to all local addresses
    addr.sin_port = htons(port);

    int flag = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0)
    {
        ERRMSG("CRpcServer", "setsockopt(SO_REUSEADDR) failed: " << strerror(errno));
        close(sock);
        return -1;
    }

    if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        ERRMSG("CRpcServer", "bind() failed" << ", strerror: " << strerror(errno)
                << ", hstrerror: " << hstrerror(h_errno));
        close(sock);
        return -1;
    }

    return sock;
}

int CRpcServer::AcceptConnection(int sock)
{
    // Accept new client connection
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);

    int fd = accept(sock, (struct sockaddr*)&addr, &alen);

    if(fd == -1)
    {
        if(errno == EINTR)
        {
            //ERRMSG("CRpcServer", "accept() failed (errno=EINTR), sock=" << sock << ": " << strerror(errno));
            ERRMSG("CRpcServer", "accept() failed (errno=EINTR): " << strerror(errno));
        }
        else
        {
            //ERRMSG("CRpcServer", "accept() failed, sock=" << sock << ": " << strerror(errno));
            ERRMSG("CRpcServer", "accept() failed: " << strerror(errno));
            sleep(5);
        }
        return -1;
    }

    int flag = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0)
    {
        ERRMSG("CRpcServer", "setsockopt(SO_REUSEADDR) failed, fd=" << fd <<": " << strerror(errno));
        close(fd);
        return -1;
    }

    flag = 1;
    if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0)
    {
        ERRMSG("CRpcServer", "setsockopt(TCP_NODELAY) failed, fd=" << fd << ": " << strerror(errno));
        close(fd);
        return -1;
    }

#ifdef SO_NOSIGPIPE
    flag = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag)) < 0)
    {
        ERRMSG("CRpcServer", "setsockopt(SO_NOSIGPIPE) failed, fd=" << fd << ": " << strerror(errno));
        close(fd);
        return -1;
    }
#endif // SO_NOSIGPIPE

//    // Get the connection host name and ip
//    char hostName[NI_MAXHOST]{};
//    if(getnameinfo((struct sockaddr*)&addr, alen, hostName, sizeof(hostName), nullptr, 0, NI_NAMEREQD) != 0)
//        strcpy(hostName, "Unknown Host Name");
//
//    const char* hostIp = inet_ntoa(addr.sin_addr);
//    if(hostIp == nullptr)
//        hostIp = "Invalid IP";

    // Let the derived class to decide it we want to proceed.
    // Note: The derived class might reset fd to 0 to do its own processing.
    // For example, to process the connection in a different thread.
    if(!OnConnection(fd) || !mContinueRunning)
    {
//        ERRMSG("CRpcServer", "Close connection from " << hostIp << " (" << hostName << ")");

        close(fd);
        return -1;
    }

//    INFOMSG("CRpcServer", "Connection (sock=" << fd <<") from " << hostIp << " (" << hostName << ")");

    return fd;
}

void CRpcServer::HandleConnection(int sock)
{
    // Create a TCP/IP-based RPC service transport and associate it with the socket.
    SVCXPRT* transp = svcfd_create(sock, 0, 0); // send_buf_size=0,rbuf_size=0
    if(transp == nullptr)
    {
        ERRMSG("CRpcServer", "svctcp_create() failed");
        close(sock);
        return;
    }

    __dispatch_fn_t dispatch = (__dispatch_fn_t)CRpcServer::RpcDispatch;

    // Associates prognum and versnum with the service dispatch procedure (dispatch),
    // but DO NOT register with the portmap service.
    if(!svc_register(transp, RPC_PROTOBUF_PROG_NUMBER, RPC_PROTOBUF_VERSION, dispatch, 0))
    {
        ERRMSG("CRpcServer", "svc_register() failed");
        svc_destroy(transp);
        close(sock);
        return;
    }

    // Non-blocking call with timeout
    struct timespec timeout = {mTimeoutSeconds,0};
    fd_set readfds;
    int res = 0;

    while(mContinueRunning)
    {
        OnNotify(NOTIFY_TYPE::WAITING_FOR_CALL);
        if(!mContinueRunning)
            break;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        res = pselect(sock+1, &readfds, nullptr, nullptr, &timeout, nullptr);
        if(res < 0)
        {
            if(errno == EINTR)
            {
                INFOMSG("CRpcServer", "pselect() interrupted with EINTR signal, continue running");
            }
            else
            {
                //ERRMSG("CRpcServer",  "pselect() failed, sock=" << sock << ": " << strerror(errno));
                // This is not an error - pselect failed because client is disconnected 
                INFOMSG("CRpcServer", "Disconnected (sock=" << sock << ")");
                break;
            }
        }
        else if(res == 0)
        {
//            INFOMSG("CRpcServer", "pselect() timed out in " << mTimeoutSeconds << " seconds, continue running");
        }
        else
        {
            svc_getreqset(&readfds);
        }
    }

    // Clean up...
    // TODO: svc_destroy() crashes when called after pselect() failed
    // so let's not call if pselect() failed
    // TODO: Could it be leaking memory?
    if(res >= 0)
        svc_destroy(transp);
    transp = nullptr;

    close(sock); // We are done with the socket

    // TODO - We don't use Portmap, do we still need to unregister?
    //svc_unregister(RPC_PROTOBUF_PROG_NUMBER, RPC_PROTOBUF_VERSION);
}

void CRpcServer::RpcDispatch(struct svc_req* rqstp, SVCXPRT* transp)
{
    switch(rqstp->rq_proc)
    {
        case NULLPROC:
            svc_sendreply(transp, (xdrproc_t)xdr_void, nullptr);
            return;
            
        case RPC_PROTOBUF_FUNC_PROC:
            break;
            
        default:
            svcerr_noproc(transp);
            return;
    }

    // Make sure that we have a valid CRpcServer object
    if(mServer == nullptr)
    {
        svcerr_systemerr(transp);
        return;
    }
    
    // Call CRpc::XdrParam to decode RPC request param
    param in, out;
    
    if(!svc_getargs(transp, (xdrproc_t)CRpc::XdrParam, (caddr_t)&in))
    {
        svcerr_decode(transp);
        return;
    }
    
    out.type = in.type; // Initially, can be reset in OnCall if desired
    
    // 1. Call CRpcServer::OnCall to handle the RPC call
    // 2. Reply with RPC response
    if(!mServer->OnCall(&in, &out))
    {
        //printf("OnCall failed\n");
        svcerr_systemerr(transp);
    }
    else if(!svc_sendreply(transp, (xdrproc_t)CRpc::XdrParam, (char*)&out))
    {
        //printf("svc_sendreply failed\n");
        svcerr_systemerr(transp);
    }
    
    // Post-reply cleanup to free the memory (if any) allocated by OnCall
    mServer->OnCleanup(&out);
    
    out.type = 0;
    out.data_len = 0;
    out.data_val = nullptr;

    // Free the memory that was allocated when RPC request param was decoded
    if(!svc_freeargs(transp, (xdrproc_t)CRpc::XdrParam, (caddr_t)&in))
    {
        std::string err;
        err = "CRpcServer::" + std::string(__func__) + ": svc_freeargs() failed to free arguments";
        mServer->LogError(err.c_str());
        //exit(1); // TODO
    }
}

void CRpcServer::GetClientInfo(int sock, std::string& clientName, std::string& clientIp)
{
    // Get the connected client host name and ip addr
    struct sockaddr addr;
    socklen_t alen = sizeof(addr);

    if(getpeername(sock, &addr, &alen) == 0)
    {
        char hostName[NI_MAXHOST]{};
        if(getnameinfo(&addr, alen, hostName, sizeof(hostName), nullptr, 0, NI_NAMEREQD) == 0)
            clientName = hostName;
        else
            clientName = "Unknown Host";

        const char* ip = inet_ntoa(((struct sockaddr_in*)&addr)->sin_addr);
        clientIp = (ip != nullptr ? ip : "Unknown IP");
    }
    else
    {
        clientName = "Unknown Host";
        clientIp = "Unknown IP";
    }
}


