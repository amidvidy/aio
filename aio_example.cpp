#include <iostream>
#include <cstring>
#include <cerrno>
#include <array>
#include <memory>
#include <utility>

#include <fcntl.h>
#include <unistd.h>
#include <libaio.h>

namespace {
  struct crash_stream  {
    ~crash_stream() {
      std::cerr << std::endl;
      std::abort();
    }
    operator bool() const { return true; }
  };

  template<typename T>
  crash_stream operator<<(crash_stream cs, T&& t) {
    std::cerr << std::forward<T>(t);
    return cs;
  }
} // namespace

#define CHECK(expr) (expr) || crash_stream()

#define SYSCALL(expr) CHECK((expr) != -1) << #expr << " failed: " << strerror(errno)

int main() {
  const char* file = "/tmp/aiotest.txt";

  std::string text = "FOOBARBAZ\n";
    // Open a file.
  int fd;
  SYSCALL(fd = open(file, O_RDWR | O_CREAT));
  for (int i = 0; i < 4096; ++i) {
    // Write a line of text to it.
    SYSCALL(write(fd, text.data(), text.size()));
  }
  // Make sure it's actually written.
  SYSCALL(fsync(fd));

  // Close the fd, and reopen with O_DIRECT.
  SYSCALL(close(fd));
  SYSCALL(fd = open(file, O_DIRECT | O_RDONLY));

  io_context_t io_ctx = 0;
  int err;

  // Create AIO queue.
  CHECK((err = io_setup(1, &io_ctx)) == 0)
    << "io_setup failed(" << err << "): "
    << strerror(err);

  std::array<struct iocb*, 1> cbs;
  struct iocb cb;

  auto pagesize = getpagesize();

  // Since AIO is O_DIRECT only, we need to align to pagesize.
  void* buf;
  posix_memalign(&buf, pagesize, pagesize);

  // Fill in our iocb.
  io_prep_pread(&cb, // iocb structure
		fd,
		buf,
		pagesize, // amount to read
		0); // offset
  cbs[0] = &cb;

  SYSCALL(io_submit(io_ctx, 1, cbs.data()));

  // passing null for the timeout makes us block. In real code, this would
  // defeat the point of using aio, but it's fine for our example.
  int ev_ct;
  std::array<struct io_event, 1> events = { 0 };
  std::cout << "res: " << events[0].res << std::endl;
  SYSCALL((ev_ct = io_getevents(io_ctx, cbs.size(), cbs.size(), events.data(), nullptr)));

  std::cout << "got " << ev_ct << std::endl;
  std::cout << "res: " << events[0].res << std::endl;
  std::cout.write(reinterpret_cast<char*>(buf), pagesize);

  SYSCALL(io_destroy(io_ctx));
  std::free(buf);
  SYSCALL(close(fd));
}
