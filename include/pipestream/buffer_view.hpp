#ifndef PIPESTREAM_BUFFER_HPP
#define PIPESTREAM_BUFFER_HPP

#include <string>

namespace pipestream
{

/* Reimplementing span like structure
 * as std::string_view is read-only but we also need to be able to write to it.
 * And std::string cannot be used as it manages its own memory.
 * Therefore this has been created as std::span only exists in C++20 and onwards 
 * (We target C++17)
 */
template<typename CharT>
class buffer_view {
public:
	using value_type = CharT;
	using reference  = CharT&;
	using const_reference = const CharT&;

	buffer_view(void) : data_(nullptr), size_(0) {}
	buffer_view(CharT *const data, const std::size_t size) : data_(data), size_(size) {}
	template<typename View>
	explicit buffer_view(View& view) : buffer_view(view.data(), view.size()) {}
	
	std::size_t size() const
	{
		return size_;
	}

	CharT* data() const
	{
		return data_;
	}

	CharT* c_str() const
	{
		return data();
	}

	bool empty() const
	{
		return 0 == size_ or nullptr == data_;
	}
private:
	CharT *const data_;
	const std::size_t size_;
};

} /* namespace pipestream */

#endif /* PIPESTREAM_BUFFER_HPP */
