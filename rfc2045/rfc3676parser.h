#ifndef	rfc3676_h
#define	rfc3676_h
/*
** Copyright 2011 S. Varshavchik.  See COPYING for
** distribution information.
*/

/*
*/

#include	"rfc2045/rfc2045_config.h"
#include	<courier-unicode.h>
#include	<string_view>
#include	<string>

namespace mail {

	// Parse a text/plain message into logical text lines.

	class textplainparser {

	public:
		// Constructor. Pass in the character set used in the message
		// and whether the message is flowed and/or delsp.
		textplainparser(
			const std::string &charset,
			bool isflowed,
			bool isdelsp
		);

		virtual ~textplainparser();

		// MIME format flowed flag set 
		const bool isflowed;

		// MIME delsp=yes flag is set
		const bool isdelsp;

		// Parsing started. Returns FALSE if the parsing could
		// not be initialized (probably unknown charset).
		bool begun() const
		{
			return uhandle != NULL;
		}

		// End parsing.
		//
		// The handle gets destroyed, and the parsing finishes.
		//
		// NOTE: rfc3676_deinit() WILL LIKELY invoke some leftover callback methods.
		//
		// Returns non-0 value returned by any callback method, or 0 if all
		// invoked callback methods returned 0.

		void end(
			// Set to true if a unicode conversion error occurred.
			bool &unicode_errflag
		);

		// A convenience version of end()
		void end()
		{
			bool dummy;

			return end(dummy);
		}

		/* Feed raw contents to be parsed */
		void operator<<(const std::string_view &text);

		// Initial parsing of flowed text format.
		//
		// Start of a logical line.
		virtual void line_begin(size_t);

		// Content of a logical line.
		virtual void line_contents(const char32_t *,size_t);

		// A physical line flowed into the next physical line. 
		virtual void line_flowed_notify();

		// End of the contents of a logical line.
		virtual void line_end();

		// Interpreting flowed logical lines

		unicode_convert_handle_t uhandle{nullptr};

		// The current error status of the parser
		bool errflag{false};

		// Set to true if the character set passed to the constructor
		// was unknown to the unicode conversion library.
		bool unknown_charset{false};

		// Receive raw text stream, converted to unicode
		size_t (textplainparser::*line_handler)(
			const char32_t *ptr,
			size_t cnt
		);

		// Receive mostly raw text stream: CRs that precede an LF
		// are removed from the stream received by content_handler.
		size_t (textplainparser::*content_handler)(
			const char32_t *ptr,
			size_t cnt
		);

		size_t quote_level{0};
		size_t sig_block_index{0};

		// Flag: previous line ended in a flowed space, and the previous
		// line's quoting level was this.
		int has_previous_quote_level{0};
		size_t previous_quote_level{0};

		// Flag: current line was flowed into from a previous line with the
		// same quoting level.
		int was_previous_quote_level{0};

		// A line has begun
		void (textplainparser::*line_begin_handler)();

		// Content of this line
		void (textplainparser::*line_content_handler)(
			const char32_t *uc,
			size_t cnt
		);

		// End of this line
		void (textplainparser::*line_end_handler)();

		// When non-flowed text is getting rewrapped, we utilize the services
		// of the unicode_lbc_info API.
		unicode_lbc_info_t lb=nullptr;

		struct unicode_buf nonflowed_line;
		// Collect unflowed line until it reaches the given size

		struct unicode_buf nonflowed_next_word;
		// Collects unicode stream until a linebreaking opportunity

		size_t nonflowed_line_target_width{0};
		// Targeted width of nonflowed lines

		size_t nonflowed_line_width{0};
		// Width of nonflowed_line

		size_t nonflowed_next_word_width{0};
		// Width of nonflowed_next_word

		// Current handle of non-flowd content.
		void (textplainparser::*nonflowed_line_process)(
			int linebreak_opportunity,
			char32_t ch,
			size_t ch_width
		);

		void (textplainparser::*nonflowed_line_end)();

		void emit_line_begin();

		void emit_line_contents(const char32_t *uc, size_t cnt);

		void emit_line_flowed_wrap();

		void emit_line_end();

		void nonflowed_line_begin();

		void nonflowed_line_contents(const char32_t *uc, size_t cnt);

		void nonflowed_line_end_default();

		static int nonflowed_line_process_trampoline(
			int linebreak_opportunity,
			char32_t ch,
			void *dummy
		);

		size_t scan_crlf(const char32_t *ptr, size_t cnt);

		size_t scan_crlf_seen_cr(const char32_t *ptr, size_t cnt);

		size_t start_of_line(const char32_t *ptr, size_t cnt);

		size_t count_quote_level(const char32_t *ptr, size_t cnt);

		size_t counted_quote_level(const char32_t *ptr, size_t cnt);

		size_t check_signature_block(const char32_t *ptr, size_t cnt);

		size_t start_content_line(const char32_t *ptr, size_t cnt);

		size_t scan_content_line(const char32_t *ptr, size_t cnt);

		size_t seen_sig_block(const char32_t *ptr, size_t cnt);

		size_t seen_notsig_block(const char32_t *ptr, size_t cnt);

		size_t seen_content_sp(const char32_t *ptr, size_t cnt);

		void initial_nonflowed_line(
			int linebreak_opportunity,
			char32_t ch,
			size_t ch_width
		);

		void initial_nonflowed_end();

		void begin_forced_rewrap();

		void forced_rewrap_line(
			int linebreak_opportunity,
			char32_t ch,
			size_t ch_width
		);

		void forced_rewrap_end();

		void check_abnormal_line();

		void emit_rewrapped_line();

	};
}

#endif
