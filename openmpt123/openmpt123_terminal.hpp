/*
 * openmpt123_terminal.hpp
 * -----------------------
 * Purpose: libopenmpt command line player
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */

#ifndef OPENMPT123_TERMINAL_HPP
#define OPENMPT123_TERMINAL_HPP

#include "openmpt123_config.hpp"

#include "mpt/base/alloc.hpp"
#include "mpt/base/detect.hpp"
#include "mpt/base/namespace.hpp"
#include "mpt/format/simple.hpp"
#include "mpt/parse/parse.hpp"
#include "mpt/string/types.hpp"
#include "mpt/string_transcode/transcode.hpp"

#include <optional>
#include <ostream>
#include <string>
#include <utility>

#include <cstddef>

#if MPT_OS_DJGPP
#include <conio.h>
#include <dpmi.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#elif MPT_OS_WINDOWS
#include <conio.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#if MPT_LIBC_MINGW
#include <string.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace openmpt123 {



template <typename Tstring>
struct concat_stream {
	virtual concat_stream & append( Tstring str ) = 0;
	virtual ~concat_stream() = default;
	inline concat_stream<Tstring> & operator<<( concat_stream<Tstring> & (*func)( concat_stream<Tstring> & s ) ) {
		return func( *this );
	}
};

template <typename Tstring>
inline concat_stream<Tstring> & lf( concat_stream<Tstring> & s ) {
	return s.append( Tstring(1, mpt::char_constants<typename Tstring::value_type>::lf) );
}

template <typename T, typename Tstring>
inline concat_stream<Tstring> & operator<<( concat_stream<Tstring> & s, const T & val ) {
	return s.append( mpt::default_formatter::template format<Tstring>( val ) );
}

template <typename Tstring>
struct string_concat_stream
	: public concat_stream<Tstring>
{
private:
	Tstring m_str;
public:
	inline void str( Tstring s ) {
		m_str = std::move( s );
	}
	inline concat_stream<Tstring> & append( Tstring s ) override {
		m_str += std::move( s );
		return *this;
	}
	inline Tstring str() const {
		return m_str;
	}
	~string_concat_stream() override = default;
};


#if MPT_OS_WINDOWS
inline std::optional<DWORD> StdHandleFromFd( int fd ) {
	std::optional<DWORD> stdHandle;
	if ( fd == _fileno( stdin ) ) {
		stdHandle = STD_INPUT_HANDLE;
	} else if ( fd == _fileno( stdout ) ) {
		stdHandle = STD_OUTPUT_HANDLE;
	} else if ( fd == _fileno( stderr ) ) {
		stdHandle = STD_ERROR_HANDLE;
	}
	return stdHandle;
}
#endif

#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
inline bool IsConsole( DWORD stdHandle ) {
	HANDLE hStd = GetStdHandle( stdHandle );
	if ( ( hStd == NULL ) || ( hStd == INVALID_HANDLE_VALUE ) ) {
		return false;
	}
	DWORD mode = 0;
	return GetConsoleMode( hStd, &mode ) != FALSE;
}
#endif // MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)

inline bool IsTerminal( int fd ) {
#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
	if ( !_isatty( fd ) ) {
		return false;
	}
	std::optional<DWORD> stdHandle = StdHandleFromFd( fd );
	if ( !stdHandle ) {
		return false;
	}
	return IsConsole( *stdHandle );
#else
	return isatty( fd ) ? true : false;
#endif
}



class textout : public string_concat_stream<mpt::ustring> {
protected:
	textout() = default;
public:
	virtual ~textout() = default;
protected:
	mpt::ustring pop() {
		mpt::ustring text = str();
		str( mpt::ustring() );
		return text;
	}
public:
	virtual void writeout() = 0;
	virtual void cursor_up( std::size_t lines ) = 0;
};



class textout_dummy : public textout {
public:
	textout_dummy() = default;
	~textout_dummy() override {
		static_cast<void>( pop() );
	}
public:
	void writeout() override {
		static_cast<void>( pop() );
	}
	void cursor_up( std::size_t lines ) override {
		static_cast<void>( lines );
	}
};



enum class transliterate_c0 {
	none,
	remove,
	ascii_replacement,
	ascii,
	unicode_replacement,
	unicode,
};

enum class transliterate_del {
	none,
	remove,
	ascii_replacement,
	ascii,
	unicode_replacement,
	unicode,
};

enum class transliterate_c1 {
	none,
	remove,
	ascii_replacement,
	unicode_replacement,
};

struct transliterate_mode {
	transliterate_c0 c0;
	transliterate_del del;
	transliterate_c1 c1;
};

inline mpt::ustring transliterate_control_codes( mpt::ustring str, transliterate_mode mode ) {
	mpt::ustring result;
	result.reserve( str.length() );
	for ( const auto c : str ) {
		if ( mpt::char_value( c ) < 0x20u ) {
			switch ( mode.c0 ) {
				case transliterate_c0::none:
					result.push_back( mpt::char_value( c ) );
					break;
				case transliterate_c0::remove:
					// nothing;
					break;
				case transliterate_c0::ascii_replacement:
					result.push_back( MPT_UCHAR('?') );
					break;
				case transliterate_c0::ascii:
					result.push_back( MPT_UCHAR('^') );
					result.push_back( static_cast<mpt::uchar>( mpt::char_value( c ) + 0x40u ) );
					break;
				case transliterate_c0::unicode_replacement:
					mpt::append( result, mpt::transcode<mpt::ustring>( std::u32string( 1, 0xfffdu ) ) );
					break;
				case transliterate_c0::unicode:
					mpt::append( result, mpt::transcode<mpt::ustring>( std::u32string( 1, mpt::char_value( c ) + 0x2400u ) ) );
					break;
			}
		} else if ( mpt::char_value( c ) == 0x7fu ) {
			switch ( mode.del ) {
				case transliterate_del::none:
					result.push_back( mpt::char_value( c ) );
					break;
				case transliterate_del::remove:
					// nothing;
					break;
				case transliterate_del::ascii_replacement:
					result.push_back( MPT_UCHAR('?') );
					break;
				case transliterate_del::ascii:
					result.push_back( MPT_UCHAR('^') );
					result.push_back( static_cast<mpt::uchar>( mpt::char_value( c ) - 0x40u ) );
					break;
				case transliterate_del::unicode_replacement:
					mpt::append( result, mpt::transcode<mpt::ustring>( std::u32string( 1, 0xfffdu ) ) );
					break;
				case transliterate_del::unicode:
					mpt::append( result, mpt::transcode<mpt::ustring>( std::u32string( 1, 0x2421u ) ) );
					break;
			}
		} else if ( ( 0x80u <= mpt::char_value( c ) ) && ( mpt::char_value( c ) <= 0x9f ) ) {
			switch ( mode.c1 ) {
				case transliterate_c1::none:
					result.push_back( mpt::char_value( c ) );
					break;
				case transliterate_c1::remove:
					// nothing;
					break;
				case transliterate_c1::ascii_replacement:
					result.push_back( MPT_UCHAR('?') );
					break;
				case transliterate_c1::unicode_replacement:
					mpt::append( result, mpt::transcode<mpt::ustring>( std::u32string( 1, 0xfffdu ) ) );
					break;
			}
		} else {
			result.push_back( mpt::char_value( c ) );
		}
	}
	return result;
}



inline mpt::ustring transliterate_control_codes_multiline( mpt::ustring str, transliterate_mode mode ) {
	if ( !str.empty() ) {
		std::vector<mpt::ustring> lines = mpt::split( str, MPT_USTRING("\n") );
		for ( auto & line : lines ) {
			line = transliterate_control_codes( line, mode );
		}
		str = mpt::join( lines, MPT_USTRING("\n") );
	}
	return str;
}



enum class textout_destination {
	destination_stdout,
	destination_stderr,
};



class textout_backend {
protected:
	textout_backend() = default;
public:
	virtual ~textout_backend() = default;
public:
	virtual void write( const mpt::ustring & text ) = 0;
	virtual void cursor_up(std::size_t lines) = 0;
};



class textout_ostream : public textout_backend {
private:
	std::ostream & s;
	void write_raw( const std::string & chars ) {
		if ( chars.size() > 0 ) {
			s.write( chars.data(), chars.size() );
		}
	}
#if MPT_OS_DJGPP
	mpt::common_encoding codepage;
#endif
public:
	textout_ostream( std::ostream & s_ )
		: s(s_)
#if MPT_OS_DJGPP
		, codepage(mpt::common_encoding::cp437)
#endif
	{
		#if MPT_OS_DJGPP
			codepage = mpt::djgpp_get_locale_encoding();
		#endif
		return;
	}
	~textout_ostream() override = default;
public:
	void write( const mpt::ustring & text ) override {
		if ( text.length() > 0 ) {
			#if MPT_OS_DJGPP
				write_raw( mpt::transcode<std::string>( codepage, transliterate_control_codes_multiline( text, { transliterate_c0::ascii, transliterate_del::ascii, transliterate_c1::ascii_replacement } ) ) );
			#elif MPT_OS_EMSCRIPTEN
				write_raw( mpt::transcode<std::string>( mpt::common_encoding::utf8, transliterate_control_codes_multiline( text, { transliterate_c0::unicode, transliterate_del::unicode, transliterate_c1::unicode_replacement } ) ) );
			#else
				write_raw( mpt::transcode<std::string>( mpt::logical_encoding::locale, transliterate_control_codes_multiline( text, { transliterate_c0::unicode, transliterate_del::unicode, transliterate_c1::unicode_replacement } ) ) );
			#endif
			s.flush();
		}	
	}
	void cursor_up( std::size_t lines ) override {
		s.flush();
		for ( std::size_t line = 0; line < lines; ++line ) {
			write_raw( std::string("\x1b[1A") );
		}
	}
};

#if MPT_OS_WINDOWS && defined(UNICODE)

class textout_wostream : public textout_backend {
private:
	std::wostream & s;
	void write_raw( const std::wstring & wchars ) {
		if ( wchars.size() > 0 ) {
			s.write( wchars.data(), wchars.size() );
		}
	}
public:
	textout_wostream( std::wostream & s_ )
		: s(s_)
	{
		return;
	}
	~textout_wostream() override = default;
public:
	void write( const mpt::ustring & text ) override {
		if ( text.length() > 0 ) {
			write_raw( mpt::transcode<std::wstring>( transliterate_control_codes_multiline( text, { transliterate_c0::unicode, transliterate_del::unicode, transliterate_c1::unicode_replacement } ) ) );
			s.flush();
		}	
	}
	void cursor_up( std::size_t lines ) override {
		s.flush();
		for ( std::size_t line = 0; line < lines; ++line ) {
			write_raw( std::wstring(L"\x1b[1A") );
		}
	}
};

#endif // MPT_OS_WINDOWS && UNICODE

#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)

class textout_ostream_console : public textout_backend {
private:
#if defined(UNICODE)
	std::wostream & s;
#else
	std::ostream & s;
#endif
	void write_raw( const mpt::winstring & wchars ) {
		if ( wchars.size() > 0 ) {
			s.write( wchars.data(), wchars.size() );
		}
	}
	HANDLE handle;
	bool console;
#if MPT_WIN_AT_LEAST(MPT_WIN_10_1809)
	DWORD mode;
#endif
public:
#if defined(UNICODE)
	textout_ostream_console( std::wostream & s_, DWORD stdHandle_ )
#else
	textout_ostream_console( std::ostream & s_, DWORD stdHandle_ )
#endif
		: s(s_)
		, handle(GetStdHandle( stdHandle_ ))
		, console(IsConsole( stdHandle_ ))
	{
		s.flush();
#if MPT_WIN_AT_LEAST(MPT_WIN_10_1809)
		if ( GetConsoleMode( handle, &mode ) == FALSE ) {
			mode = 0;
		}
#endif
		return;
	}
	~textout_ostream_console() override = default;
public:
	void write( const mpt::ustring & text ) override {
		if ( text.length() > 0 ) {
			if ( console ) {
				DWORD chars_written = 0;
				mpt::winstring wtext = mpt::transcode<mpt::winstring>( transliterate_control_codes_multiline( text, { transliterate_c0::unicode, transliterate_del::unicode, transliterate_c1::unicode_replacement } ) );
				WriteConsole( handle, wtext.data(), static_cast<DWORD>( wtext.size() ), &chars_written, NULL );
			} else {
				write_raw( mpt::transcode<mpt::winstring>( transliterate_control_codes_multiline( text, { transliterate_c0::unicode, transliterate_del::unicode, transliterate_c1::unicode_replacement } ) ) );
				s.flush();
			}
		}
	}
	void cursor_up( std::size_t lines ) override {
		if ( console ) {
			#if MPT_WIN_AT_LEAST(MPT_WIN_10_1809)
				if ( ( mode & (ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING) ) == (ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING) ) {
					for ( std::size_t line = 0; line < lines; ++line ) {
						DWORD chars_written = 0;
						mpt::winstring wtext = mpt::winstring(TEXT("\x1b[A"));
						WriteConsole( handle, wtext.data(), static_cast<DWORD>( wtext.size() ), &chars_written, NULL );
					}
					return;
				}
			#endif
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			ZeroMemory( &csbi, sizeof( CONSOLE_SCREEN_BUFFER_INFO ) );
			COORD coord_cursor = COORD();
			if ( GetConsoleScreenBufferInfo( handle, &csbi ) != FALSE ) {
				coord_cursor = csbi.dwCursorPosition;
				coord_cursor.X = 1;
				coord_cursor.Y -= static_cast<SHORT>( lines );
				SetConsoleCursorPosition( handle, coord_cursor );
			}
		} else {
			s.flush();
			for ( std::size_t line = 0; line < lines; ++line ) {
				write_raw( mpt::winstring(TEXT("\x1b[1A")) );
			}
		}
	}
};

#endif // MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)



template <textout_destination dest>
class textout_wrapper : public textout {
private:
#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
#if defined(UNICODE)
	textout_ostream_console out{ dest == textout_destination::destination_stdout ? std::wcout : std::wclog, dest == textout_destination::destination_stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE };
#else
	textout_ostream_console out{ dest == textout_destination::destination_stdout ? std::cout : std::clog, dest == textout_destination::destination_stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE };
#endif
#elif MPT_OS_WINDOWS
#if defined(UNICODE)
	textout_wostream out{ dest == textout_destination::destination_stdout ? std::wcout : std::wclog };
#else
	textout_ostream out{ dest == textout_destination::destination_stdout ? std::cout : std::clog };
#endif
#else
	textout_ostream out{ dest == textout_destination::destination_stdout ? std::cout : std::clog };
#endif
public:
	textout_wrapper() = default;
	~textout_wrapper() override {
		out.write( pop() );
	}
public:
	void writeout() override {
		out.write( pop() );
	}
	void cursor_up(std::size_t lines) override {
		out.cursor_up( lines );
	}
};


inline void query_terminal_size( int & terminal_width, int & terminal_height ) {
#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
		HANDLE hStdOutput = GetStdHandle( STD_OUTPUT_HANDLE );
		if ( ( hStdOutput != NULL ) && ( hStdOutput != INVALID_HANDLE_VALUE ) ) {
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			ZeroMemory( &csbi, sizeof( CONSOLE_SCREEN_BUFFER_INFO ) );
			if ( GetConsoleScreenBufferInfo( hStdOutput, &csbi ) != FALSE ) {
				if ( terminal_width <= 0 ) {
					terminal_width = std::min( static_cast<int>( 1 + csbi.srWindow.Right - csbi.srWindow.Left ), static_cast<int>( csbi.dwSize.X ) );
				}
				if ( terminal_height <= 0 ) {
					terminal_height = std::min( static_cast<int>( 1 + csbi.srWindow.Bottom - csbi.srWindow.Top ), static_cast<int>( csbi.dwSize.Y ) );
				}
			}
		}
#else // !(MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10))
		if ( isatty( STDERR_FILENO ) ) {
			if ( terminal_width <= 0 ) {
				const char * env_columns = std::getenv( "COLUMNS" );
				if ( env_columns ) {
					int tmp = mpt::parse_or<int>( env_columns, 0 );
					if ( tmp > 0 ) {
						terminal_width = tmp;
					}
				}
			}
			if ( terminal_height <= 0 ) {
				const char * env_rows = std::getenv( "ROWS" );
				if ( env_rows ) {
					int tmp = mpt::parse_or<int>( env_rows, 0 );
					if ( tmp > 0 ) {
						terminal_height = tmp;
					}
				}
			}
			#if defined(TIOCGWINSZ)
				struct winsize ts;
				if ( ioctl( STDERR_FILENO, TIOCGWINSZ, &ts ) >= 0 ) {
					if ( terminal_width <= 0 ) {
						terminal_width = ts.ws_col;
					}
					if ( terminal_height <= 0 ) {
						terminal_height = ts.ws_row;
					}
				}
			#elif defined(TIOCGSIZE)
				struct ttysize ts;
				if ( ioctl( STDERR_FILENO, TIOCGSIZE, &ts ) >= 0 ) {
					if ( terminal_width <= 0 ) {
						terminal_width = ts.ts_cols;
					}
					if ( terminal_height <= 0 ) {
						terminal_height = ts.ts_rows;
					}
				}
			#endif
		}
#endif // MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
#if MPT_OS_DJGPP
		if ( terminal_width <= 0 ) {
			terminal_width = 80;
		}
		if ( terminal_height <= 0 ) {
			terminal_height = 25;
		}
#else
		if ( terminal_width <= 0 ) {
			terminal_width = 72;
		}
		if ( terminal_height <= 0 ) {
			terminal_height = 23;
		}
#endif
}


#if MPT_OS_WINDOWS

class terminal_ui_guard {
public:
	terminal_ui_guard() = default;
	terminal_ui_guard( const terminal_ui_guard & ) = delete;
	terminal_ui_guard( terminal_ui_guard && ) = default;
	terminal_ui_guard & operator=( const terminal_ui_guard & ) = delete;
	terminal_ui_guard & operator=( terminal_ui_guard && ) = default;
	~terminal_ui_guard() = default;
};

#else

class terminal_ui_guard {
private:
	bool changed = false;
	termios saved_attributes;
public:
	terminal_ui_guard() {
		if ( !isatty( STDIN_FILENO ) ) {
			return;
		}
		tcgetattr( STDIN_FILENO, &saved_attributes );
		termios tattr = saved_attributes;
		tattr.c_lflag &= ~( ICANON | ECHO );
		tattr.c_cc[VMIN] = 1;
		tattr.c_cc[VTIME] = 0;
		tcsetattr( STDIN_FILENO, TCSAFLUSH, &tattr );
		changed = true;
	}
	terminal_ui_guard( const terminal_ui_guard & ) = delete;
	terminal_ui_guard( terminal_ui_guard && ) = default;
	terminal_ui_guard & operator=( const terminal_ui_guard & ) = delete;
	terminal_ui_guard & operator=( terminal_ui_guard && ) = default;
	~terminal_ui_guard() {
		if ( changed ) {
			tcsetattr(STDIN_FILENO, TCSANOW, &saved_attributes);
		}
	}
};

#endif


class terminal_input {
public:
	static inline bool is_input_available() {
#if MPT_OS_DJGPP
		return kbhit() ? true : false;
#elif MPT_OS_WINDOWS && defined( UNICODE ) && !MPT_OS_WINDOWS_WINRT
		return _kbhit() ? true : false;
#elif MPT_OS_WINDOWS
		return _kbhit() ? true : false;
#else
		pollfd pollfds;
		pollfds.fd = STDIN_FILENO;
		pollfds.events = POLLIN;
		poll(&pollfds, 1, 0);
		if ( !( pollfds.revents & POLLIN ) ) {
			return false;
		}
		return true;
#endif
	}
	static inline std::optional<int> read_input_char() {
#if MPT_OS_DJGPP
		int c = getch();
		return static_cast<int>( c );
#elif MPT_OS_WINDOWS && defined( UNICODE ) && !MPT_OS_WINDOWS_WINRT
		wint_t c = _getwch();
		return static_cast<int>( c );
#elif MPT_OS_WINDOWS
		int c = _getch();
		return static_cast<int>( c );
#else
		char c = 0;
		if ( read( STDIN_FILENO, &c, 1 ) != 1 ) {
			return std::nullopt;
		}
		return static_cast<int>( c );
#endif
	}
};


} // namespace openmpt123

#endif // OPENMPT123_TERMINAL_HPP
