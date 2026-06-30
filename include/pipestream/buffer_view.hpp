#ifndef PIPESTREAM_BUFFER_HPP
#define PIPESTREAM_BUFFER_HPP

#include <string>

namespace pipestream
{

/* Reimplementing span like structure
 * as std::string_view is read-only but we also need to be able to write to it.
 * And std::string cannot be used as it manages its own memory.
 * Therefore this has been created as std::span only exists in C++20 and onwards
 */
template<typename CharT>
class buffer_view {
public:
	using value_type = CharT;
	using reference  = CharT&;
	using const_reference = const CharT&;

	buffer_view(CharT *const data, const std::size_t size) : data_(data), size_(size) {}
	template<typename Traits>
	explicit buffer_view(std::basic_string_view<CharT, Traits> str) : buffer_view(str.data(), str.size()) {}
	template<typename Traits, typename Alloc>
	explicit buffer_view(std::basic_string<CharT, Traits, Alloc>& str) : buffer_view(str.data(), str.size()) {}
	
	std::size_t size() const
	{
		return size_;
	}

	CharT* data() const
	{
		return data_;
	}

	bool empty() const
	{
		if (0 == size_ or nullptr == data_) {
			return true;
		}
		return false;
	}
private:
	const std::size_t size_;
	CharT *const data_;
};

} /* namespace pipestream */

#endif /* PIPESTREAM_BUFFER_HPP */
