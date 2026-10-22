#include <windows.h>
#include "../Examples/worker_protocol.h"
#include <iostream>
// Test-only: the controller deliberately terminates this process while the
// incoming connection's shared allocator lock is held. Never package this app.
int main(int argc,char** argv){
    if(argc!=2)return 1;
    try {
        Transport transport;
        auto servers=transport.AcceptServers(argv[1],[](auto& connection){
            auto memory=connection.GetInputChannel().GetMemory();
            return [memory](Request,auto&&){
                memory->InvokeAtomic([]{std::cout<<"LOCKED"<<std::endl;Sleep(INFINITE);});
            };
        });
        std::cout<<"READY"<<std::endl;std::string line;std::getline(std::cin,line);return 0;
    }catch(...){return 1;}
}
