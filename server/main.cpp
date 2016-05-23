#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <iomanip>
#include <sys/mman.h>
#include "unique_hld_definitions_linux.hpp"
#include "scope_exit.hpp"
#include "config.hpp"

namespace jetbrains {

namespace {

bool volatile _Oursig_int = false;

void sigint_handler(int _Sig_num)
{
    if (_Sig_num == SIGINT)
        _Oursig_int = true;
}

}

}


int main()
{
    using namespace jetbrains;

    std::cout << "Server" << std::endl;
    common::unique_hld_close const _Socket_fd(socket(AF_UNIX, SOCK_STREAM, 0));
    if (!_Socket_fd)
    {
        perror("Failed to open socket");
        return 1;
    }

    sockaddr_un _Sa_un;
    memset(&_Sa_un, 0, sizeof(_Sa_un));
    _Sa_un.sun_family = AF_UNIX;
    strcpy(_Sa_un.sun_path, P_tmpdir);
    strcat(_Sa_un.sun_path, _Ourun_path);
    unlink(_Sa_un.sun_path);
    if (bind(_Socket_fd.get(), reinterpret_cast<sockaddr const *>(&_Sa_un), sizeof(_Sa_un)) < 0)
    {
        perror("Failed to bind socket");
        return 1;
    }

    if (listen(_Socket_fd.get(), 10) < 0)
    {
        perror("Failed to listen socket");
        return 1;
    }

    std::cout << "Press Ctrl+C to stop..." << std::endl;

    struct sigaction _Sig;
    memset(&_Sig, 0, sizeof(_Sig));
    _Sig.sa_handler = &sigint_handler;
    if (sigaction(SIGINT, &_Sig, nullptr) < 0)
    {
        perror("Failed to set SIGINT handler");
        return 1;
    }

    while (!_Oursig_int)
    {
        timeval _Tv;
        _Tv.tv_sec = 1;
        _Tv.tv_usec = 0;

        fd_set _Rfds;
        FD_ZERO(&_Rfds);
        FD_SET(_Socket_fd.get(), &_Rfds);
        if (select(_Socket_fd.get() + 1, &_Rfds, nullptr, nullptr, &_Tv) < 0)
            if (errno == EINTR)
                continue;
            else
            {
                perror("Failed to select socket");
                return 1;
            }

        if (FD_ISSET(_Socket_fd.get(), &_Rfds))
        {
            common::unique_hld_close const _Accept_fd(accept(_Socket_fd.get(), nullptr, nullptr));
            if (!_Accept_fd)
            {
                perror("Failed to accept socket");
                return 1;
            }

            common::unique_hld_close _File_fd;

            {
                pid_t _Client_pid;
                iovec _Iov;
                memset(&_Iov, 0, sizeof(_Iov));
                _Iov.iov_base = &_Client_pid;
                _Iov.iov_len = sizeof(_Client_pid);

                unsigned char _Acm[1024];
                msghdr _Msg;
                memset(&_Msg, 0, sizeof(_Msg));
                _Msg.msg_iov = &_Iov;
                _Msg.msg_iovlen = 1;
                _Msg.msg_control = _Acm;
                _Msg.msg_controllen = sizeof(_Acm);

                auto const _Size = recvmsg(_Accept_fd.get(), &_Msg, 0);
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

                std::cout << "Client pid=" << _Client_pid << std::endl;

                for (auto _Cm = CMSG_FIRSTHDR(&_Msg); _Cm; _Cm = CMSG_NXTHDR(&_Msg, _Cm))
                    if (_Cm->cmsg_type == SCM_RIGHTS)
                    {
                        _File_fd.reset(*reinterpret_cast<int *>(CMSG_DATA(&_Acm)));
                        break;
                    }

                if (!_File_fd)
                {
                    perror("Failed to check file socket");
                    return 1;
                }
            }

            {
                auto const _Page_size = sysconf(_SC_PAGESIZE);
                auto const _Ptr = mmap(nullptr, _Page_size, PROT_WRITE, MAP_SHARED, _File_fd.get(), 0);
                if (_Ptr == MAP_FAILED)
                {
                    perror("Failed to map file");
                    return 1;
                }
                common::scope_exit const _On_exit_munmap([_Ptr, _Page_size] () { munmap(_Ptr, _Page_size); });

#pragma pack(push, 1)
                static struct data
                {
                    uint32_t _Mysize;
                    unsigned char _Mydata[3];
                } const _Ourdata = { sizeof(data::_Mydata), { 0xAA, 0xBB, 0xCC }};
#pragma pack(pop)

                if (ftruncate(_File_fd.get(), sizeof(_Ourdata)) < 0)
                {
                    perror("Failed to resize file");
                    return 1;
                }

                memcpy(static_cast<data *>(_Ptr), &_Ourdata, sizeof(_Ourdata));
            }

            {
                auto const _Pid = getpid();
                iovec _Iov;
                memset(&_Iov, 0, sizeof(_Iov));
                _Iov.iov_base = const_cast<pid_t *>(&_Pid);
                _Iov.iov_len = sizeof(_Pid);

                msghdr _Msg;
                memset(&_Msg, 0, sizeof(_Msg));
                _Msg.msg_iov = &_Iov;
                _Msg.msg_iovlen = 1;

                auto const _Size = sendmsg(_Accept_fd.get(), &_Msg, 0);
                if (_Size < 0)
                {
                    perror("Failed to send data to socket");
                    return 1;
                }
                else if (_Size != sizeof(pid_t))
                {
                    perror("Invalid send message data size");
                    return 1;
                }
            }
        }
    }

    std::cout << "Done" << std::endl;
    return 0;
}
