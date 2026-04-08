#ifndef PIPESTREAM_PIPEBUF_HPP
#define PIPESTREAM_PIPEBUF_HPP

#include <algorithm>
#include <ios>
#include <streambuf>

#include <unistd.h>

#include "unique_fd.hpp"

namespace pipestream
{

class basic_pipebuf : public std::streambuf {
public:
	basic_pipebuf(unique_fd&& fd) : pipe_fd(std::move(fd))
	{
		// TODO buf
		set_buf_ptrs(new char_type[4096], 4096);
	}

	virtual ~basic_pipebuf() override
	{
		if (pipe_fd.has_fd()) {
			if (pptr() not_eq pbase()) {
				flush();
			}
			::close(pipe_fd.get());
		}
		// TODO free buffer
	}

	basic_pipebuf(const basic_pipebuf&) = delete;
	basic_pipebuf(basic_pipebuf&& other) : pipe_fd(std::move(other.pipe_fd))
	{
		// TODO
	}

	basic_pipebuf& operator=(const basic_pipebuf&) = delete;
	basic_pipebuf& operator=(basic_pipebuf&& other)
	{
		// TODO
		return *this;
	}

protected:
	virtual basic_pipebuf* setbuf(char_type* s, std::streamsize n) override
	{
		set_buf_ptrs(s, n);
		return this;
	}

	virtual std::streamsize showmanyc() override
	{
		// TODO
		return 0;
	}

	virtual int_type underflow() override
	{
		if (not pipe_fd.has_fd()) {
			return traits_type::eof();
		}
		const std::streamsize n_chars = ::read(pipe_fd.get(), eback(), egptr()-eback());
		if (n_chars <= 0) {
			return traits_type::eof();
		}
		setg(eback(), eback(), eback()+n_chars);
		return traits_type::to_int_type(*gptr());
	}

	virtual int_type overflow(int_type ch = traits_type::eof()) override
	{
		// Add last character ch to buffer, now its really full
		*gptr() = ch;
		pbump(1);
		if (flush()) {
			return traits_type::eof();
		}
		// TODO check EOF? behavior
		if (traits_type::eq_int_type(ch, traits_type::eof())) {
			return traits_type::not_eof(ch);
		}
		return ch;
	}

	virtual int_type pbackfail(int_type c = traits_type::eof()) override
	{
		// TODO
		return std::streambuf::pbackfail(c);
	}

	virtual int sync() override
	{
		if (flush()) {
			return -1;
		}
		return 0;
	}

private:
	unique_fd pipe_fd;
	char_type* buf;

	bool flush()
	{
		if (not pipe_fd.has_fd()) {
			return true;
		}
		const std::streamsize n_chars = ::write(pipe_fd.get(), pbase(), pptr()-pbase());
		if (n_chars <= 0) {
			// return true on failure
			return true;
		}
		if (n_chars not_eq pptr()-pbase()) {
			// not handling edge case when not all characters could be written into pipe
		}
		// TODO why epptr()?
		setp(buf, epptr());
		return false;
	}

	void set_buf_ptrs(char_type* new_buf, const std::streamsize n)
	{
		if (nullptr == new_buf && n != 0) {
			// fail -- logic error
		}
		if (nullptr != new_buf && n <= 0) {
			// fail -- logic error
		}
		buf = new_buf;
		setg(buf, buf, buf);
		setp(buf, buf+n); // leaving one extra space free for last character from underflow()
	}
};

} /* namespace pipestream */

#endif /* PIPESTREAM_PIPEBUF_HPP */
