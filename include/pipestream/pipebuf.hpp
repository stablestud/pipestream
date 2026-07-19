#ifndef PIPESTREAM_PIPEBUF_HPP
#define PIPESTREAM_PIPEBUF_HPP

#include <algorithm>
#include <cstring>
#include <ios>
#include <memory>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <type_traits>

#include <unistd.h>

#include "buffer_view.hpp"
#include "fd.hpp"

namespace pipestream
{

template<typename T>
struct is_multibyte : std::bool_constant<(sizeof(T)>1)> {};

template<typename T>
constexpr bool is_multibyte_v = is_multibyte<T>::value;

template<typename CharT, typename Traits = std::char_traits<CharT>, typename Fd = fd>
class basic_pipebuf : public std::basic_streambuf<CharT, Traits> {
public:
	using typename std::basic_streambuf<CharT, Traits>::char_type;
	using typename std::basic_streambuf<CharT, Traits>::traits_type;
	using typename std::basic_streambuf<CharT, Traits>::int_type;
	using typename std::basic_streambuf<CharT, Traits>::off_type;
	using typename std::basic_streambuf<CharT, Traits>::pos_type;

	using std::basic_streambuf<CharT,Traits>::eback;
	using std::basic_streambuf<CharT,Traits>::pbase;
	using std::basic_streambuf<CharT,Traits>::gptr;
	using std::basic_streambuf<CharT,Traits>::pptr;
	using std::basic_streambuf<CharT,Traits>::epptr;
	using std::basic_streambuf<CharT,Traits>::egptr;

	using buffer_view_type = pipestream::buffer_view<char_type>;
	using string_type      = std::basic_string<char_type, traits_type>;
	using string_view_type = std::basic_string_view<char_type, traits_type>;

	basic_pipebuf(Fd&& move_fd) : pipe_fd(std::move(move_fd))
	{
		constexpr std::size_t DEFAULT_BUFFER_SIZE = 4096;
		std::unique_ptr<char_type[]> new_buf = std::make_unique<char_type[]>(DEFAULT_BUFFER_SIZE);
		set_buf_ptrs(new_buf.get(), DEFAULT_BUFFER_SIZE);
		self_managed = std::move(new_buf);
	}

	virtual ~basic_pipebuf() override
	{
		buffer_flush();
	}

	basic_pipebuf(const basic_pipebuf<CharT, Traits, Fd>&) = delete;
	basic_pipebuf(basic_pipebuf<CharT, Traits, Fd>&& other) noexcept : std::basic_streambuf<CharT, Traits>(std::move(other)), pipe_fd(std::move(other.pipe_fd)), self_managed(std::move(other.self_managed))
	{
		move_scalars(std::move(other));
	}

	basic_pipebuf<CharT, Traits, Fd>& operator=(const basic_pipebuf<CharT, Traits, Fd>&) = delete;
	basic_pipebuf<CharT, Traits, Fd>& operator=(basic_pipebuf<CharT, Traits, Fd>&& other) noexcept
	{
		if (this not_eq &other) {
			std::basic_streambuf<CharT, Traits>::operator=(std::move(other));
			self_managed = std::move(other.self_managed);
			pipe_fd = std::move(other.pipe_fd);
			move_scalars(std::move(other));
		}
		return *this;
	}

	void swap(basic_pipebuf<CharT, Traits, Fd>& other) noexcept
	{
		std::swap(*this, other);
	}

	Fd& rdfd() noexcept
	{
		return pipe_fd;
	}
	Fd& rdfd(Fd&& new_fd) noexcept
	{
		return pipe_fd = std::move(new_fd);
	}

	static inline constexpr std::streamsize default_buf_size = 4096;

protected:
	virtual basic_pipebuf<CharT, Traits, Fd>* setbuf(char_type* s, const std::streamsize n) override
	{
		set_buf_ptrs(s, n);
		return this;
	}

	virtual std::streamsize xsputn(const char_type* s, const std::streamsize count) override
	{
		if (0 == count) {
			return count;
		}
		if (nullptr == s or 0 > count) {
			throw std::invalid_argument("invalid sputn arguments");
		}
		const string_view_type client_buf(s, count);
		if (nullptr == buf) {
			// No buffer available write directly into pipe
			return pipe_write(client_buf);
		}
		const std::streamsize buf_writeable = pptr()-pbase();
		if ((epptr() - pptr()) >= count) {
			// String fits into current buffer, copy string to buffer
			std::memcpy(pptr(), client_buf.data(), client_buf.size()*sizeof(char_type));
			this->pbump(count);
			return count;
		}
		if (0 < buf_writeable) {
			// Buffer not empty and string does not fit into buffer,
			// combine buffer and string and write directly to pipe
			string_type aux{};
			aux.append(pbase(), buf_writeable);
			aux.append(client_buf);
			const auto written = pipe_write(aux);
			if (0 >= written) {
				// Nothing has been written to pipe
				return 0;
			}
			if (written == buf_writeable+client_buf.size()) {
				// Successfully written buffer and string to pipe
				reset_put_buf();
				return client_buf.size();
			}
			if (0 < buf_writeable-written) {
				// Partially written buffer
				this->setp(pbase()+written, epptr()); // Remove written chars from buffer
				this->pbump(buf_writeable-written);
				return 0;
			}
			// Buffer has been fully written, but not string
			reset_put_buf();
			return written-buf_writeable;
		}
		// Write directly into pipe as buffer is empty and string does not fit into buffer
		return pipe_write(client_buf);
	}

	virtual std::streamsize xsgetn(char_type* s, const std::streamsize count) override
	{
		if (0 == count) {
			return 0;
		}
		if (nullptr == s or 0 > count) {
			throw std::invalid_argument("invalid sgetn arguments");
		}
		buffer_view_type client_buf{s, static_cast<std::size_t>(count)};
		if (nullptr == buf) {
			// No buffer available read directly from pipe
			return pipe_read(client_buf);
		}
		const std::streamsize buf_readable = egptr()-gptr();
		if (0 < buf_readable and buf_readable >= client_buf.size()) {
			// Data is fully available in buffer, copy from buffer to array
			std::memcpy(client_buf.data(), gptr(), client_buf.size()*sizeof(char_type));
			this->gbump(client_buf.size());
			return client_buf.size();
		}
		if (0 < buf_readable) {
			// Data is partially available in buffer
			std::memcpy(client_buf.data(), gptr(), buf_readable*sizeof(char_type));
			this->gbump(buf_readable);
			// Read missing data from pipe
			return this->xsgetn(client_buf.data()+buf_readable, client_buf.size()-buf_readable)+buf_readable;
		}
		if (buf_size/2 > client_buf.size()) {
			// Populate buffer and reread from buffer if requested size is half length of buffer
			// This avoids usage of auxilary buffer and therefore avoids copying from aux to internal buffer on small requests
			if (0 == buffered_internal_read()) {
				return 0;
			}
			return this->xsgetn(s, count);
		}
		// Optimization: reduce syscalls by using aux buffer if requested size is atleast half size of internal buffer
		// With that we try to fetch as much data as possible with as single requst without leaving the internal buffer half consumed
		const auto n_chars = buffered_aux_read(client_buf);
		if (client_buf.size() > n_chars) {
			// Less than required count have been read, try again
			return this->xsgetn(client_buf.data()+n_chars, client_buf.size()-n_chars)+n_chars;
		}
		return client_buf.size();
	}

	virtual int_type underflow() override
	{
		if (nullptr == buf) {
			// No buffer to populate, read single character directly from pipe
			char_type c;
			buffer_view_type cview{&c, 1};
			if (0 >= pipe_read(cview)) {
				return traits_type::eof();
			}
			return traits_type::to_int_type(c);
		}
		if (0 == buffered_internal_read()) {
			return traits_type::eof();
		}
		return traits_type::to_int_type(*gptr());
	}

	virtual int_type uflow()
	{
		if (nullptr == buf) {
			// Required to prevent sbumpc to sigfault on nullptr buffer
			return underflow();
		}
		return std::basic_streambuf<char_type, Traits>::uflow();
	}

	virtual int_type overflow(const int_type ch = traits_type::eof()) override
	{
		if (nullptr == buf) {
			const char_type c{traits_type::to_char_type(ch)};
			string_view_type cv(&c, 1);
			if (not pipe_write(cv)) {
				return traits_type::eof();
			}
		} else {
			if (not traits_type::eq_int_type(ch, traits_type::eof())) {
				// Add last character ch to buffer if its not EOF, now buffer is really full
				*pptr() = ch;
				this->pbump(1);
			}
			if (!buffer_flush()) {
				// Flushing has failed
				this->pbump(-1); // Need to make room for future writes, dropping last character added
				return traits_type::eof(); // Notify client that write of last character has failed
			}
		}
		if (traits_type::eq_int_type(ch, traits_type::eof())) {
			return traits_type::not_eof(ch);
		}
		return ch;
	}

	virtual int_type pbackfail(const int_type c = traits_type::eof()) override
	{
		if (nullptr == buf) {
			// Putback/unget operations not supported on nullptr
			return traits_type::eof();
		}
		if (traits_type::eq_int_type(c, traits_type::eof())) {
			return traits_type::eof();
		}
		const std::streamsize buf_area = gptr()-eback();
		if (0 >= buf_area) {
			// No writeable buffer area available
			return traits_type::eof();
		}
		this->gbump(-1);
		*gptr() = c;
		return c;
	}

	virtual int sync() override
	{
		if (!buffer_flush()) {
			// Flush buffer failed
			return -1;
		}
		return 0;
	}
private:
	Fd pipe_fd;
	std::size_t buf_size; 
	char_type* buf;
	std::size_t partial{};
	std::unique_ptr<char_type[]> self_managed;

	void set_buf_ptrs(char_type* new_buf, const std::streamsize n)
	{
		if ((nullptr == new_buf and n != 0) or (nullptr != new_buf and n < 2)) {
			throw std::invalid_argument("invalid setbuf arguments");
		}
		if (self_managed) {
			self_managed.reset();
		}
		buf = new_buf;
		buf_size = n;
		if (nullptr == buf) {
			this->setp(nullptr, nullptr);
			this->setg(nullptr, nullptr, nullptr);
		} else {
			reset_put_buf();
			this->setg(buf+buf_size, buf+buf_size, buf+buf_size);
		}
	}

	bool buffer_flush()
	{
		if (pbase() == pptr()) {
			// Nothing to flush, return early
			// Also applies for nullptr buf
			return true;
		}
		const auto buf_writeable = pptr()-pbase();
		const string_view_type buf_view(pbase(), buf_writeable);
		const auto n_chars = pipe_write(buf_view);
		if (not n_chars) {
			// Nothing was written, failed write
			return false;
		}
		if (n_chars not_eq buf_writeable) {
			// Not all characters could be written
			this->setp(pbase()+n_chars, epptr()); // Remove written chars from buffer
			this->pbump(buf_writeable-n_chars);
			return false; // returning "error" to let client know flushing was not completely successful
		}
		// Successfully flushed whole buffer
		reset_put_buf();
		return true;
	}

	std::size_t buffered_internal_read()
	{
		char_type putback;
		std::streamsize offset{};
		if (egptr() not_eq eback()) {
			// Buffer is populated (not empty),
			// therefore we need to persist last buffer entry for putback 
			putback = *(egptr()-1);
			offset = 1;
		}
		buffer_view_type buf_view{buf+offset, buf_size-offset};
		const auto n_chars = pipe_read(buf_view);
		if (0 >= n_chars) {
			// No data were read from pipe, indicating EOF
			return 0;
		}
		if (offset not_eq 0) {
			// Needed for putback/unget
			*buf = putback;
		}
		this->setg(buf, buf_view.data(), buf_view.data()+n_chars);
		return n_chars;
	}

	std::size_t buffered_aux_read(buffer_view_type client_buf)
	{
		string_type aux(client_buf.size()+buf_size-1L, char_type{}); // -1 for putback copy
		const auto n_chars = pipe_read(buffer_view_type{aux});
		if (0 >= n_chars) {
			// No data were read from pipe, indicating EOF
			return 0;
		}
		std::memcpy(client_buf.data(), aux.data(), std::min(n_chars, client_buf.size())*sizeof(char_type));
		if (0 <= n_chars-client_buf.size()) {
			// Enough data for count has been read
			*buf = *(aux.data()+client_buf.size()-1); // Prepare putback space
			const int extra = n_chars-client_buf.size();
			if (0 < extra) {
				// More data than needed for count has been read,
				// move extra data into buffer
				std::memcpy(buf+1, aux.data()+client_buf.size(), extra*sizeof(char_type));
			}
			this->setg(buf, buf+1, buf+1+extra);
		}
		return n_chars;
	}

	std::size_t pipe_read(buffer_view_type buf_view)
	{
		if (not pipe_fd.has_valid_fd() or buf_view.empty()) {
			// Pipe or buffer not correctly setup, return early
			return 0;
		}
		const auto n_bytes = pipe_fd.read(buf_view);
		if (0 >= n_bytes) {
			return 0;
		}
		const auto n_chars = n_bytes/sizeof(char_type);
		if constexpr (is_multibyte_v<char_type>) {
			if (0 != (partial = (n_bytes+partial)%sizeof(char_type))) {
				// A character has been partially read
			}
		}
		return n_chars;
	}

	std::size_t pipe_write(const string_view_type buf_view)
	{
		if (not pipe_fd.has_valid_fd()) {
			// Pipe is not correctly setup, return early
			return 0;
		}
		const auto n_bytes = pipe_fd.write(buf_view);
		if (0 >= n_bytes) {
			return 0;
		}
		const auto n_chars = n_bytes/sizeof(char_type);
		if constexpr (is_multibyte_v<char_type>) {
			if (0 != (partial = (n_bytes+partial)%sizeof(char_type))) {
				// A character has been partially written
			}
			// Need to ensure that ojn partial write it is still counted as full char written on return type
			// if partial written, put the rest into buffer and setup ptr and partial counter to note how much is available in buffer and where
			// but what to do if second write is also partial, so if previous was partial, and this one also (n-partial)/sizeof(char_type) than we had again a partial write, then setup partial and buf and ptr again
			// A character has been partially written
			return (n_bytes+partial)/sizeof(char_type);
		}
		return n_chars;
	}

	void reset_put_buf() noexcept
	{
		// Leaving one extra space free for last character from underflow()
		this->setp(buf, buf+buf_size-1);
	}

	void move_scalars(basic_pipebuf<char_type, Traits, Fd>&& other) noexcept
	{
		buf_size = other.buf_size;
		buf = other.buf;
		other.buf_size = 0;
		other.buf = nullptr;
		other.setg(nullptr, nullptr, nullptr);
		other.setp(nullptr, nullptr);
	}
};

template<typename CharT, typename Traits, typename Fd>
void swap(basic_pipebuf<CharT, Traits, Fd>& left, basic_pipebuf<CharT, Traits, Fd>& right)
{
	left.swap(right);
}

// Added this helper function as CTAD does not work with explicit template arguments
// i.e. basic_pipebuf<char>(fd) would not use CTAD, therefore using this helper function instead
template<typename CharT = char, typename Fd>
constexpr basic_pipebuf<CharT, std::char_traits<CharT>, Fd> make_pipebuf(Fd&& fd)
{
	return basic_pipebuf<CharT, std::char_traits<CharT>, Fd>(std::move(fd));
}

template<typename Fd>
constexpr basic_pipebuf<wchar_t, std::char_traits<wchar_t>, Fd> make_wpipebuf(Fd&& fd)
{
	return make_pipebuf<wchar_t>(std::forward<Fd>(fd));
}

using pipebuf = basic_pipebuf<char>;
using wpipebuf = basic_pipebuf<wchar_t>;

} /* namespace pipestream */

#endif /* PIPESTREAM_PIPEBUF_HPP */
