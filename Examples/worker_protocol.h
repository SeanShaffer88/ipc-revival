#pragma once
#include <IPC/Transport.h>
#include <boost/container/vector.hpp>
#include <cstdint>
using Blob=boost::container::vector<std::uint8_t,IPC::SharedMemory::Allocator<std::uint8_t>>;
struct Request { std::uint64_t id; IPC::SharedMemory::SharedPtr<Blob> blob; };
struct Response { std::uint64_t id; std::uint64_t size; std::uint64_t checksum; };
using Transport=IPC::Transport<Request,Response>;
constexpr std::size_t MaxBytes=256*1024;
inline std::uint64_t Hash(const std::uint8_t* bytes,std::size_t size) {
    std::uint64_t hash=14695981039346656037ull;
    for(std::size_t i=0;i<size;++i) { hash^=bytes[i]; hash*=1099511628211ull; }
    return hash;
}
