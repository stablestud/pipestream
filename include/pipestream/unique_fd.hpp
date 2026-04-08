#ifndef PIPESTREAM_UNIQUE_FD_HPP
#define PIPESTREAM_UNIQUE_FD_HPP

#include <unistd.h>

using fd_type = int;

namespace pipestream
{
	class unique_fd {
	public:
		explicit unique_fd(fd_type fd) : fd(fd) {};
		~unique_fd() noexcept
		{
			_close();
		}
		unique_fd(const unique_fd&) = delete;
		unique_fd(unique_fd&& other) noexcept : fd(other.fd)
		{
			other.fd = NONE;
		}

		unique_fd& operator=(const unique_fd&) = delete;
		unique_fd& operator=(unique_fd&& other) noexcept
		{
			if (this not_eq &other) {
				fd = other.fd;
				other.fd = NONE;
			}
			return *this;
		}

		fd_type get() const
		{
			return fd;
		}

		fd_type release()
		{
			const fd_type ffd = fd;
			fd = NONE;
			return ffd;
		}

		bool close()
		{
			if (_close()) {
				// TODO if failure
				return true;
			}
			fd = NONE;
			return false;
		}

		bool has_fd()
		{
			if (fd not_eq NONE) {
				return true;
			}
			return false;
		}

		static inline constexpr fd_type NONE = -1;
	private:
		fd_type fd = NONE;

		int _close()
		{
			if (fd not_eq NONE) {
				return ::close(fd);
			}
			return 1;
		}
	};
}

#endif /* PIPESTREAM_UNIQUE_FD_HPP */
