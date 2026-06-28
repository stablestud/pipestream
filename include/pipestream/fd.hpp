#ifndef PIPESTREAM_FD_HPP
#define PIPESTREAM_FD_HPP

#include <unistd.h>

namespace pipestream
{
	using fd_type = int;

	class fd {
	public:
		explicit fd(fd_type basic_fd) : basic_fd(basic_fd) {};
		virtual ~fd() noexcept
		{
			close();
		}

		fd(const fd&) = delete;
		fd(fd&& other) noexcept : basic_fd(other.basic_fd)
		{
			other.basic_fd = none;
		}

		fd& operator=(const fd&) = delete;
		fd& operator=(fd&& other) noexcept
		{
			if (this not_eq &other) {
				basic_fd = other.basic_fd;
				other.basic_fd = none;
			}
			return *this;
		}
		virtual bool operator==(const fd& other) const
		{
			return basic_fd == other.basic_fd;
		}

		virtual bool operator!=(const fd& other) const
		{
			return not operator==(other);
		}

		fd_type get() const
		{
			return basic_fd;
		}

		virtual fd_type release()
		{
			const fd_type ffd = basic_fd;
			basic_fd = none;
			return ffd;
		}

		virtual bool close()
		{
			if (none == basic_fd) {
				return true;
			}
			int ret = ::close(basic_fd);
			basic_fd = none;
			return not ret;
		}

		virtual bool has_valid_fd() const
		{
			if (basic_fd not_eq none) {
				return true;
			}
			return false;
		}

		virtual std::streamsize read(void *buf, const std::streamsize n_bytes) const
		{
			if (not has_valid_fd()) {
				return -1;
			}
			return ::read(basic_fd, buf, n_bytes);
		}

		virtual std::streamsize write(const void *buf, const std::streamsize n_bytes) const
		{
			if (not has_valid_fd()) {
				return -1;
			}
			return ::write(basic_fd, buf, n_bytes);
		}

		static inline constexpr fd_type none = -1;
	protected:
		fd_type basic_fd = none;
	};
}

#endif /* PIPESTREAM_FD_HPP */
