/*
*/
#ifndef	rfc822_encode_h
#define	rfc822_encode_h

/*
** Copyright 2004 Double Precision, Inc.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"rfc822/config.h"
#endif
#include	<stdio.h>
#include	<sys/types.h>
#include	<stdlib.h>
#include	<time.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct libmail_encode_info {
	char output_buffer[BUFSIZ];
	int output_buf_cnt;

	char input_buffer[57]; /* For base64 */
	int input_buf_cnt;

	int (*encoding_func)(struct libmail_encode_info *,
			     const char *, size_t);
	int (*callback_func)(const char *, size_t, void *);
	void *callback_arg;
};

const char *libmail_encode_autodetect_fp(FILE *, int, int *);
const char *libmail_encode_autodetect_fpoff(FILE *, int, off_t, off_t, int *);
const char *libmail_encode_autodetect_buf(const char *, int);

void libmail_encode_start(struct libmail_encode_info *info,
			  const char *transfer_encoding,
			  int (*callback_func)(const char *, size_t, void *),
			  void *callback_arg);

int libmail_encode(struct libmail_encode_info *info,
		   const char *ptr,
		   size_t cnt);

int libmail_encode_end(struct libmail_encode_info *info);

#ifdef  __cplusplus
}

#include <type_traits>
#include <utility>

namespace rfc822 {

	// C++ wrapper for libmail_encode()
	//
	// This is a template, and the template parameters get deduced from
	// the constructor's parameter.
	//
	// The first parameter is an object that's callable with a const char *
	// and a size_t parameter, and it gets called, repeatedly, with the
	// encoded contents, a chunk at a time (the output object).
	//
	// If the output object is passed by reference, then the reference
	// to the object is saved, and the referenced object must exist
	// as long as the encode object is used. If the first parameter is
	// passed by value a copy of it gets copied/moved into encode. The
	// encode object instance has a conversion operator that returns the
	// copy of the output chunk-receiving object, which can be called after
	// end() to retrieve its final value, if it has any meaning.
	//
	// The second parameter specifies the content transfer encoding.
	//
	// The encode instance is called repeatedly, with const char * and
	// size_t parameter to define the unencoded content, in chunks.
	// end() gets called to formally define the end of the unencoded
	// content. The encoded content is buffered internally and end() will
	// likely result in the output object getting called with the final
	// encoded contents. The destructor also calls end().

	template<typename out_iter_type>
	class encode : private libmail_encode_info {

		std::conditional_t<
			std::is_same_v<out_iter_type, out_iter_type &>,
			out_iter_type,
			std::remove_cv_t<
				std::remove_reference_t<out_iter_type>>
			> out_iter;

	public:
		template<typename T>
		encode(T &&out_iter, const char *encoding)
			: out_iter{std::forward<T>(out_iter)}
		{
			libmail_encode_start(
				this, encoding,
				trampoline,
				reinterpret_cast<void *>(this)
			);
		}

		static int trampoline(const char *ptr,
				      size_t l,
				      void *voidptr)
		{
			auto me=reinterpret_cast<encode<out_iter_type> *>(
				voidptr
			);
			me->out_iter(ptr, l);
			return 0;
		}

		void operator()(const char *ptr, size_t cnt)
		{
			libmail_encode(this, ptr, cnt);
		}

		void end()
		{
			libmail_encode_end(this);
		}

		~encode()
		{
			end();
		}

		operator std::remove_cv_t<
			std::remove_reference_t<out_iter_type>>() const
		{
			return out_iter;
		}

		encode &operator=(const encode &)=delete;
		encode(const encode &)=delete;
	};

	template<typename T>
	encode(T &&out_iter, const char *) -> encode<T &&>;

	template<typename T>
	encode(T &out_iter, const char *) -> encode<T &>;
}

#define rfc2045_encode_h_included 1
#include "rfc2045_encode.h"
#endif

#endif
