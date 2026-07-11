#ifndef PIPESTREAM_TESTS_MOCKS_HPP
#define PIPESTREAM_TESTS_MOCKS_HPP

#include <ios>
#include <string>

#include <doctest/doctest.h>
#include <doctest/trompeloeil.hpp>

#include <pipestream/fd.hpp>

#include "testutils.hpp"

namespace pipestream::mocks
{
	class mocked_fd : public pipestream::fd {
	public:
		explicit mocked_fd(const pipestream::fd_type input_fd) : pipestream::fd(input_fd) {};
		explicit mocked_fd() : pipestream::fd(testutils::DUMMY_FD) {};
		static constexpr bool trompeloeil_movable_mock = true;
		MAKE_MOCK0(close, bool(void), override);
		MAKE_CONST_MOCK2(read, std::streamsize(void*, const std::streamsize), override);
		MAKE_CONST_MOCK2(write, std::streamsize(const void*, const std::streamsize), override);

		template<typename Buffer>
		std::streamsize read(Buffer& buf)
		{
			return pipestream::fd::read<Buffer>(buf);
		}

		template<typename BufferView>
		std::streamsize write(const BufferView bufv)
		{
			return pipestream::fd::write<BufferView>(bufv);
		}
	};

	template<typename T>
	void flush_mocked(pipestream::basic_pipebuf<T, std::char_traits<T>, mocked_fd>& buf)
	{
		// This function is required, in the case when mocked read is forbidden,
		// before deconstructor run to flush/empty the pipebuf buffer,
		// then flushing does not need to be done in the deconstructor,
		// that would trigger forbidden read. Thus we temporarily allow reading here
		auto m = NAMED_ALLOW_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::_))
				.RETURN(_2);
		buf.pubsync();
	}
}

#endif /* PIPESTREAM_TESTS_MOCKS_HPP */
