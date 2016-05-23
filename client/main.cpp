#include <memory>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <iomanip>
#include "unique_hld_definitions_linux.hpp"
#include "scope_exit.hpp"
#include "config.hpp"

int main()
{
    using namespace jetbrains;

    std::cout << "Client" << std::endl;

    char _Temp_name[] = P_tmpdir "/memory_mapped_file_sharing_XXXXXX";
    common::unique_hld_close const _File_fd(mkstemp(_Temp_name));
    if (!_File_fd)
    {
        perror("Failed to open temp file");
        return 1;
    }
    common::scope_exit const _On_exit_unlink([&_Temp_name] () { unlink(_Temp_name); });

    auto const _Page_size = sysconf(_SC_PAGESIZE);
    auto const _Ptr = mmap(nullptr, _Page_size, PROT_READ, MAP_SHARED, _File_fd.get(), 0);
    if (_Ptr == MAP_FAILED)
    {
        perror("Failed to map file");
        return 1;
    }
    common::scope_exit const _On_exit_munmap([_Ptr, _Page_size] () { munmap(_Ptr, _Page_size); });

    common::unique_hld_close const _Socket_fd(socket(AF_UNIX, SOCK_STREAM, 0));
    if (!_Socket_fd)
    {
        perror("Failed to open socket");
        return 1;
    }

    {
        sockaddr_un _Sa_un;
        memset(&_Sa_un, 0, sizeof(_Sa_un));
        _Sa_un.sun_family = AF_UNIX;
        strcpy(_Sa_un.sun_path, P_tmpdir);
        strcat(_Sa_un.sun_path, _Ourun_path);
        if (connect(_Socket_fd.get(), reinterpret_cast<sockaddr const *>(&_Sa_un), sizeof(_Sa_un)) < 0)
        {
            perror("Failed to connect to server");
            return 1;
        }
    }

    {
#pragma pack(push, 1)
        union
        {
            cmsghdr _Mycm;
            unsigned char _Myblob[CMSG_SPACE(sizeof(int))];
        } _Acm;
#pragma pack(pop)
        memset(&_Acm, 0, sizeof(_Acm));
        _Acm._Mycm.cmsg_len = CMSG_LEN(sizeof(int));
        _Acm._Mycm.cmsg_level = SOL_SOCKET;
        _Acm._Mycm.cmsg_type = SCM_RIGHTS;
        *reinterpret_cast<int *>(CMSG_DATA(&_Acm)) = _File_fd.get();

        auto const _Pid = getpid();
        iovec _Iov;
        memset(&_Iov, 0, sizeof(_Iov));
        _Iov.iov_base = const_cast<pid_t *>(&_Pid);
        _Iov.iov_len = sizeof(_Pid);

        msghdr _Msg;
        memset(&_Msg, 0, sizeof(_Msg));
        _Msg.msg_iov = &_Iov;
        _Msg.msg_iovlen = 1;
        _Msg.msg_control = _Acm._Myblob;
        _Msg.msg_controllen = sizeof(_Acm._Myblob);

        auto const _Size = sendmsg(_Socket_fd.get(), &_Msg, 0);
        if (_Size < 0)
        {
            perror("Failed to sent data to socket");
            return 1;
        }
        else if (_Size != sizeof(pid_t))
        {
            perror("Invalid send message data size");
            return 1;
        }
    }

    {
        pid_t _Server_pid;
        iovec _Iov;
        _Iov.iov_base = &_Server_pid;
        _Iov.iov_len = sizeof(_Server_pid);

        msghdr _Msg;
        memset(&_Msg, 0, sizeof(_Msg));
        _Msg.msg_iov = &_Iov;
        _Msg.msg_iovlen = 1;

        auto const _Size = recvmsg(_Socket_fd.get(), &_Msg, 0);
        if (_Size < 0)
        {
            perror("Failed to receive data from socket");
            return 1;
        }
        else if (_Size != sizeof(pid_t))
        {
            perror("Invalid receive message data size");
            return 1;
        }

        std::cout << "Server pid=" << _Server_pid << std::endl;
    }

    {
        auto const _Pos = lseek(_File_fd.get(), 0, SEEK_CUR);
        if (_Pos < 0)
        {
            perror("Failed to get current file position");
            return 1;
        }
        else if (_Pos != static_cast<off_t>(0))
        {
            perror("Invalid current file position");
            return 1;
        }
    }

    {
        std::cout << std::hex << std::setw(2) << std::setfill('0');

#pragma pack(push, 1)
        struct data
        {
            uint32_t _Mysize;
            unsigned char _Mydata[1];
        } const * const _Data = static_cast<data const *>(_Ptr);
#pragma pack(pop)

        for (uint32_t _N = 0; _N < _Data->_Mysize; ++_N)
        {
            if (_N != 0)
                std::cout << ", ";
            std::cout << "0x" << static_cast<unsigned>(_Data->_Mydata[_N]);
        }
        std::cout << std::endl;
    }

    return 0;
}