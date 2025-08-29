#include "file_object.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace SAK {

namespace {

#ifdef _WIN32
using FileHandleType = HANDLE;

inline FileHandleType OpenRO(const std::string& path) {
    HANDLE hFile = ::CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    return (hFile == INVALID_HANDLE_VALUE) ? nullptr : hFile;
}

inline FileHandleType CreateFile(const std::string& path, const std::vector<uint8_t>& data) {
    HANDLE hFile = ::CreateFileA(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    
    DWORD bytesWritten = 0;
    if (!::WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr) || 
        bytesWritten != data.size()) {
        ::CloseHandle(hFile);
        return nullptr;
    }
    
    ::CloseHandle(hFile);
    return OpenRO(path);
}

inline uint64_t FileSize(FileHandleType handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    
    LARGE_INTEGER size;
    if (!::GetFileSizeEx(handle, &size)) {
        return 0;
    }
    return static_cast<uint64_t>(size.QuadPart);
}

#else
using FileHandleType = int;

inline FileHandleType OpenRO(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    return (fd < 0) ? -1 : fd;
}

inline FileHandleType CreateFile(const std::string& path, const std::vector<uint8_t>& data) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    
    ssize_t written = ::write(fd, data.data(), data.size());
    if (written < 0 || static_cast<size_t>(written) != data.size()) {
        ::close(fd);
        return -1;
    }
    
    ::close(fd);
    return OpenRO(path);
}

inline uint64_t FileSize(FileHandleType fd) {
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}
#endif
}  // namespace

class FileHandle {
public:
#ifdef _WIN32
    explicit FileHandle(HANDLE handle) : handle_(handle) {}
    ~FileHandle() {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
    }
#else
    explicit FileHandle(int fd) : fd_(fd) {}
    ~FileHandle() {
        if (fd_ >= 0) ::close(fd_);
    }
#endif

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle(FileHandle&& other) noexcept {
        move_from(other);
    }
    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            cleanup();
            move_from(other);
        }
        return *this;
    }

#ifdef _WIN32
    HANDLE Handle() const noexcept { return handle_; }
#else
    int Fd() const noexcept { return fd_; }
#endif

private:
#ifdef _WIN32
    HANDLE handle_{nullptr};
    
    void move_from(FileHandle& other) noexcept {
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    
    void cleanup() {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
            handle_ = nullptr;
        }
    }
#else
    int fd_{-1};
    
    void move_from(FileHandle& other) noexcept {
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    
    void cleanup() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
#endif
};

struct FileObjectImpl {
    FileHandle handle;
    uint64_t size;
    
    FileObjectImpl(FileHandle h, uint64_t s) : handle(std::move(h)), size(s) {}
};


FileObject FileObject::Create(const std::string& path, const std::vector<uint8_t>& data) {
    auto handle = CreateFile(path, data);
    uint64_t size = data.size();
    FileObject obj;
    obj.impl_ = std::make_shared<FileObjectImpl>(FileHandle(handle), size);
    return obj;
}

FileObject FileObject::Open(const std::string& path) {
    auto handle = OpenRO(path);
    uint64_t size = FileSize(handle);
    FileObject obj;
    obj.impl_ = std::make_shared<FileObjectImpl>(FileHandle(handle), size);
    return obj;
}

std::vector<uint8_t> FileObject::Read(uint64_t offset, uint64_t len) const {
    if (!Valid()) {
        return {};  // Return empty vector for invalid file
    }
    
    std::vector<uint8_t> buf(len);
    
#ifdef _WIN32
    HANDLE hFile = impl_->handle.Handle();
    if (hFile == nullptr || hFile == INVALID_HANDLE_VALUE) {
        return {};
    }
    
    LARGE_INTEGER liOffset;
    liOffset.QuadPart = static_cast<LONGLONG>(offset);
    
    if (!::SetFilePointerEx(hFile, liOffset, nullptr, FILE_BEGIN)) {
        return {};
    }
    
    DWORD bytesRead = 0;
    if (!::ReadFile(hFile, buf.data(), static_cast<DWORD>(len), &bytesRead, nullptr) || 
        bytesRead != len) {
        return {};
    }
#else
    ssize_t n = ::pread(impl_->handle.Fd(), buf.data(), len, static_cast<off_t>(offset));
    if (n < 0 || static_cast<uint64_t>(n) != len) {
        return {};  // Return empty vector on read failure
    }
#endif
    
    return buf;
}

bool FileObject::Valid() const noexcept { return impl_ != nullptr; }
uint64_t FileObject::Size() const noexcept { return Valid() ? impl_->size : 0; }

} // namespace SAK
