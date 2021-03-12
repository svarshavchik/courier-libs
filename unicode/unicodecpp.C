/*
** Copyright 2011-2021 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"

#include <algorithm>
#include <exception>
#include <new>


namespace {
#if 0
}
#endif

template<typename callable>
struct cb_wrapper {

	const std::function<callable> &cb;
	std::exception_ptr caught;

	cb_wrapper(const std::function<callable> &cb) : cb{cb}
	{
	}

	template<typename ...Args> void operator()(Args && ...args)
	{
		if (caught)
			return;
		try {
			cb(std::forward<Args>(args)...);
		} catch (...)
		{
			caught=std::current_exception();
		}
	}

	void rethrow()
	{
		if (caught)
			std::rethrow_exception(caught);
	}
};
#if 0
{
#endif
}


extern "C" {

	static int iconv_trampoline(const char *str, size_t cnt, void *arg)
	{
		return reinterpret_cast<unicode::iconvert *>(arg)
			->converted(str, cnt);
	}

	int unicode::linebreak_trampoline(int value, void *ptr)
	{
		return (*reinterpret_cast<unicode::linebreak_callback_base *>
			(ptr)).callback(value);
	}

	int unicode::linebreakc_trampoline(int value, char32_t ch, void *ptr)
	{
		return (*reinterpret_cast<unicode::linebreakc_callback_base *>
			(ptr)).callback(value, ch);
	}

	int unicode::wordbreak_trampoline(int value, void *ptr)
	{
		return (*reinterpret_cast<unicode::wordbreak_callback_base *>
			(ptr)).callback(value != 0);
	}

}

const char unicode::ucs_4[]=
#if WORDS_BIGENDIAN
	"UCS-4BE"
#else
	"UCS-4LE"
#endif
	;

const char unicode::ucs_2[]=
#if WORDS_BIGENDIAN
	"UCS-2BE"
#else
	"UCS-2LE"
#endif
	;

const char unicode::utf_8[]="utf-8";

const char unicode::iso_8859_1[]="iso-8859-1";

// Initialize unicode_default_chset() at thread startup.

namespace unicode {

	class init_chset {
	public:
		init_chset();
	};
};

unicode::init_chset::init_chset()
{
	unicode_default_chset();
}

size_t unicode_wcwidth(const std::u32string &uc)
{
	size_t w=0;

	for (std::u32string::const_iterator
		     b(uc.begin()), e(uc.end()); b != e; ++b)
		w += unicode_wcwidth(*b);
	return w;
}

unicode::iconvert::iconvert() : handle(NULL)
{
}

unicode::iconvert::~iconvert()
{
	end();
}

int unicode::iconvert::converted(const char *, size_t)
{
	return 0;
}

bool unicode::iconvert::begin(const std::string &src_chset,
			   const std::string &dst_chset)
{
	end();

	if ((handle=unicode_convert_init(src_chset.c_str(),
					   dst_chset.c_str(),
					   &iconv_trampoline,
					   this)) == NULL)
		return false;
	return true;
}

bool unicode::iconvert::end(bool *errflag)
{
	int errptr;

	int rc;

	if (!handle)
		return true;

	rc=unicode_convert_deinit(handle, &errptr);
	handle=NULL;

	if (errflag)
		*errflag=errptr != 0;
	return rc == 0;
}

bool unicode::iconvert::operator()(const char *str, size_t cnt)
{
	if (!handle)
		return false;

	return (unicode_convert(handle, str, cnt) == 0);
}

bool unicode::iconvert::operator()(const char32_t *str, size_t cnt)
{
	if (!handle)
		return false;

	return (unicode_convert_uc(handle, str, cnt) == 0);
}

std::string unicode::iconvert::convert(const std::string &text,
				    const std::string &charset,
				    const std::string &dstcharset,
				    bool &errflag)
{
	std::string buf;
	int errptr;

	char *p=unicode_convert_tobuf(text.c_str(),
					charset.c_str(),
					dstcharset.c_str(),
					&errptr);

	errflag= errptr != 0;

	try {
		buf=p;
		free(p);
	} catch (...) {
		free(p);
		throw;
	}

	return buf;
}


std::string unicode::iconvert::convert(const std::u32string &uc,
				    const std::string &dstcharset,
				    bool &errflag)
{
	std::string buf;

	char *c;
	size_t csize;
	int err;

	if (uc.empty())
	{
		errflag=false;
		return buf;
	}

	if (unicode_convert_fromu_tobuf(&uc[0], uc.size(),
					  dstcharset.c_str(), &c, &csize,
					  &err))
	{
		err=1;
	}
	else
	{
		if (csize)
			--csize; // Trailing NULL
		try {
			buf.append(c, c+csize);
			free(c);
		} catch (...)
		{
			free(c);
			throw;
		}
	}

	errflag= err != 0;

	return buf;
}

bool unicode::iconvert::convert(const std::string &text,
			     const std::string &charset,
			     std::u32string &uc)
{
	int err;

	char32_t *ucbuf;
	size_t ucsize;

	if (unicode_convert_tou_tobuf(text.c_str(),
					text.size(),
					charset.c_str(),
					&ucbuf,
					&ucsize,
					&err))
		return false;

	try {
		uc.clear();
		uc.reserve(ucsize);
		uc.insert(uc.end(), ucbuf, ucbuf+ucsize);
		free(ucbuf);
	} catch (...)
	{
		free(ucbuf);
		throw;
	}

	return err == 0;
}

int unicode::iconvert::tou::converted(const char32_t *, size_t)
{
	return 0;
}

bool unicode::iconvert::tou::begin(const std::string &chset)
{
	return iconvert::begin(chset, unicode_u_ucs4_native);
}

int unicode::iconvert::tou::converted(const char *ptr, size_t cnt)
{
	return converted(reinterpret_cast<const char32_t *>(ptr),
			 cnt/sizeof(char32_t));
}

std::pair<std::u32string, bool>
unicode::iconvert::tou::convert(const std::string &str,
				const std::string &chset)
{
	std::pair<std::u32string, bool> ret;

	ret.second=convert(str.begin(), str.end(), chset, ret.first);
	return ret;
}

bool unicode::iconvert::fromu::begin(const std::string &chset)
{
	return iconvert::begin(unicode_u_ucs4_native, chset);
}

std::pair<std::string, bool>
unicode::iconvert::fromu::convert(const std::u32string &ubuf,
				  const std::string &chset)
{
	std::pair<std::string, bool> ret;

	convert(ubuf.begin(), ubuf.end(), chset,
		ret.first, ret.second);

	return ret;
}

std::string unicode::iconvert::convert_tocase(const std::string &text,
					   const std::string &charset,
					   bool &err,
					   char32_t (*first_char_func)(char32_t),
					   char32_t (*char_func)(char32_t))
{
	err=false;
	std::string s;

	char *p=unicode_convert_tocase(text.c_str(),
					 charset.c_str(),
					 first_char_func,
					 char_func);

	if (!p)
	{
		err=true;
		return s;
	}

	try {
		s=p;
		free(p);
	} catch (...) {
		free(p);
		throw;
	}
	return s;
}

unicode::linebreak_callback_base::linebreak_callback_base()
	: handle(NULL), opts(0)
{
}


void unicode::linebreak_callback_base::set_opts(int optsArg)
{
	opts=optsArg;

	if (handle)
		unicode_lb_set_opts(handle, opts);
}

unicode::linebreak_callback_base::~linebreak_callback_base()
{
	finish();
}

int unicode::linebreak_callback_base::callback(int ignore)
{
	return 0;
}

unicode::linebreak_callback_base
&unicode::linebreak_callback_base::operator<<(char32_t uc)
{
	if (!handle)
	{
		handle=unicode_lb_init(linebreak_trampoline,
				       reinterpret_cast<void *>
				       (static_cast<linebreak_callback_base *>
					(this)));
		set_opts(opts);
	}

	if (handle)
		if (unicode_lb_next(handle, uc))
			finish();
	return *this;
}

void unicode::linebreak_callback_base::finish()
{
	if (handle)
		unicode_lb_end(handle);
	handle=NULL;
}


unicode::linebreak_callback_save_buf::linebreak_callback_save_buf()
{
}

unicode::linebreak_callback_save_buf::~linebreak_callback_save_buf()
{
}

int unicode::linebreak_callback_save_buf::callback(int value)
{
	lb_buf.push_back(value);
	return 0;
}

unicode::linebreakc_callback_base::linebreakc_callback_base()
	: handle(NULL), opts(0)
{
}

unicode::linebreakc_callback_base::~linebreakc_callback_base()
{
	finish();
}

int unicode::linebreakc_callback_base::callback(int dummy1, char32_t dummy2)
{
	return 0;
}

void unicode::linebreakc_callback_base::set_opts(int optsArg)
{
	opts=optsArg;

	if (handle)
		unicode_lbc_set_opts(handle, opts);
}

unicode::linebreakc_callback_base
&unicode::linebreakc_callback_base::operator<<(char32_t uc)
{
	if (handle == NULL)
	{
		handle=unicode_lbc_init(linebreakc_trampoline,
					reinterpret_cast<void *>
					(static_cast<linebreakc_callback_base *>
					 (this)));
		set_opts(opts);
	}

	if (handle)
		if (unicode_lbc_next(handle, uc))
			finish();
	return *this;
}

void unicode::linebreakc_callback_base::finish()
{
	if (handle)
		unicode_lbc_end(handle);
	handle=NULL;
}


unicode::linebreakc_callback_save_buf::linebreakc_callback_save_buf()
{
}

unicode::linebreakc_callback_save_buf::~linebreakc_callback_save_buf()
{
}

int unicode::linebreakc_callback_save_buf::callback(int c, char32_t ch)
{
	lb_buf.push_back(std::make_pair(c, ch));
	return 0;
}

unicode::wordbreak_callback_base::wordbreak_callback_base()
	: handle(NULL)
{
}

unicode::wordbreak_callback_base::~wordbreak_callback_base()
{
	finish();
}

int unicode::wordbreak_callback_base::callback(bool ignore)
{
	return 0;
}

unicode::wordbreak_callback_base
&unicode::wordbreak_callback_base::operator<<(char32_t uc)
{
	if (!handle)
	{
		handle=unicode_wb_init(wordbreak_trampoline,
				       reinterpret_cast<void *>
				       (static_cast<wordbreak_callback_base *>
					(this)));
	}

	if (handle)
		if (unicode_wb_next(handle, uc))
			finish();
	return *this;
}

void unicode::wordbreak_callback_base::finish()
{
	if (handle)
		unicode_wb_end(handle);
	handle=NULL;
}

/* -------------------------------------------- */

unicode::wordbreakscan::wordbreakscan() : handle(NULL)
{
}

unicode::wordbreakscan::~wordbreakscan()
{
	finish();
}

bool unicode::wordbreakscan::operator<<(char32_t uc)
{
	if (!handle)
		handle=unicode_wbscan_init();

	if (handle)
		return unicode_wbscan_next(handle, uc) != 0;

	return false;
}

size_t unicode::wordbreakscan::finish()
{
	size_t n=0;

	if (handle)
	{
		n=unicode_wbscan_end(handle);
		handle=NULL;
	}
	return n;
}

std::string unicode::tolower(const std::string &string)
{
	return tolower(string, unicode_default_chset());
}

std::string unicode::tolower(const std::string &string,
			     const std::string &charset)
{
	std::u32string uc;

	unicode::iconvert::convert(string, charset, uc);

	return unicode::iconvert::convert(tolower(uc), charset);
}

std::u32string unicode::tolower(const std::u32string &u)
{
	std::u32string copy=u;

	std::transform(copy.begin(), copy.end(), copy.begin(), unicode_lc);
	return copy;
}

std::string unicode::toupper(const std::string &string)
{
	return toupper(string, unicode_default_chset());
}

std::string unicode::toupper(const std::string &string,
			     const std::string &charset)
{
	std::u32string uc;

	unicode::iconvert::convert(string, charset, uc);

	return unicode::iconvert::convert(toupper(uc), charset);
}

std::u32string unicode::toupper(const std::u32string &u)
{
	std::u32string copy=u;

	std::transform(copy.begin(), copy.end(), copy.begin(), unicode_uc);

	return copy;
}


unicode::bidi_calc_types::bidi_calc_types(const std::u32string &s)
	: s{s}
{
	types.resize(s.size());
	if (!s.empty())
		unicode_bidi_calc_types(s.c_str(), s.size(), &types[0]);
}

unicode::bidi_calc_types::~bidi_calc_types()=default;

void unicode::bidi_calc_types::setbnl(std::u32string &s)
{
	if (s.empty() || s.size() != types.size())
		return;

	unicode_bidi_setbnl(&s[0], &types[0], s.size());
}

std::tuple<std::vector<unicode_bidi_level_t>,
	   struct unicode_bidi_direction>
unicode::bidi_calc(const bidi_calc_types &s)
{
	return unicode::bidi_calc(s, UNICODE_BIDI_SKIP);
}

std::tuple<std::vector<unicode_bidi_level_t>,
	   struct unicode_bidi_direction>
unicode::bidi_calc(const bidi_calc_types &st,
		   unicode_bidi_level_t paragraph_embedding_level)
{
	std::tuple<std::vector<unicode_bidi_level_t>,
		   struct unicode_bidi_direction>
		ret;
	auto &direction_ret=std::get<1>(ret);

	if (st.s.size() != st.types.size())
	{
		direction_ret.direction=UNICODE_BIDI_LR;
		direction_ret.is_explicit=false;
		return ret;
	}

	const unicode_bidi_level_t *initial_embedding_level=0;

	if (paragraph_embedding_level == UNICODE_BIDI_LR ||
	    paragraph_embedding_level == UNICODE_BIDI_RL)
	{
		initial_embedding_level=&paragraph_embedding_level;
	}

	std::get<0>(ret).resize(st.s.size());

	if (initial_embedding_level)
	{
		direction_ret.direction=paragraph_embedding_level;
		direction_ret.is_explicit=1;
	}
	else
	{
		direction_ret.direction= UNICODE_BIDI_LR;
	}

	if (st.s.size())
	{
		std::get<1>(ret)=
			unicode_bidi_calc_levels(st.s.c_str(),
						 &st.types[0],
						 st.s.size(),
						 &std::get<0>(ret)[0],
						 initial_embedding_level);
	}
	return ret;
}

extern "C" {
	static void reorder_callback(size_t i, size_t cnt,
				     void *arg)
	{
		auto p=reinterpret_cast<cb_wrapper<void (size_t,
							 size_t)> *>(arg);

		(*p)(i, cnt);
	}
}

int unicode::bidi_reorder(std::u32string &string,
			  std::vector<unicode_bidi_level_t> &levels,
			  const std::function<void (size_t, size_t)> &lambda,
			  size_t pos,
			  size_t n)
{
	size_t s=string.size();

	if (s != levels.size())
		return -1;

	if (pos >= s)
		return 0;

	if (n > s-pos)
		n=s-pos;
	cb_wrapper<void (size_t, size_t)> cb{lambda};

	unicode_bidi_reorder(&string[pos], &levels[pos], n,
			     reorder_callback,
			     reinterpret_cast<void *>(&cb));

	cb.rethrow();
	return 0;
}

void unicode::bidi_reorder(std::vector<unicode_bidi_level_t> &levels,
			   const std::function<void (size_t, size_t)> &lambda,
			   size_t pos,
			   size_t n)
{
	size_t s=levels.size();

	if (!s)
		return;

	if (pos >= s)
		return;

	if (n > s-pos)
		n=s-pos;

	cb_wrapper<void (size_t, size_t)> cb{lambda};

	unicode_bidi_reorder(0, &levels[pos], n, reorder_callback,
			     reinterpret_cast<void *>(&cb));
	cb.rethrow();

}

extern "C" {
	static void removed_callback(size_t i,
				     void *arg)
	{
		auto p=reinterpret_cast<cb_wrapper<void (size_t)> *>(arg);

		(*p)(i);
	}
}

void unicode::bidi_cleanup(std::u32string &string,
			   const std::function<void (size_t)> &lambda,
			   int cleanup_options)
{
	if (string.empty())
		return;

	cb_wrapper<void (size_t)> cb{lambda};

	size_t n=unicode_bidi_cleanup(&string[0],
				      0,
				      string.size(),
				      cleanup_options,
				      removed_callback,
				      reinterpret_cast<void *>(&cb));
	cb.rethrow();
	string.resize(n);
}

int unicode::bidi_cleanup(std::u32string &string,
			  std::vector<unicode_bidi_level_t> &levels,
			  const std::function<void (size_t)> &lambda,
			  int cleanup_options)
{
	if (levels.size() != string.size())
		return -1;

	if (levels.size() == 0)
		return 0;

	cb_wrapper<void (size_t)> cb{lambda};
	size_t n=unicode_bidi_cleanup(&string[0],
				      &levels[0],
				      string.size(),
				      cleanup_options,
				      removed_callback,
				      reinterpret_cast<void *>(&cb));
	cb.rethrow();

	string.resize(n);
	levels.resize(n);
	return 0;
}

int unicode::bidi_cleanup(std::u32string &string,
			  std::vector<unicode_bidi_level_t> &levels,
			  const std::function<void (size_t)> &lambda,
			  int cleanup_options,
			  size_t starting_pos,
			  size_t n)
{
	size_t s=string.size();

	if (levels.size() != s)
		return -1;

	if (starting_pos >= s)
		return 0;

	if (n > s-starting_pos)
		n=s-starting_pos;

	cb_wrapper<void (size_t)> cb{lambda};
	unicode_bidi_cleanup(&string[starting_pos],
			     &levels[starting_pos],
			     n,
			     cleanup_options,
			     removed_callback,
			     reinterpret_cast<void *>(&cb));
	cb.rethrow();
	return 0;
}

int unicode::bidi_logical_order(std::u32string &string,
				std::vector<unicode_bidi_level_t> &levels,
				unicode_bidi_level_t paragraph_embedding,
				const std::function<void (size_t, size_t)>
				&lambda,
				size_t starting_pos,
				size_t n)
{
	auto s=string.size();

	if (s != levels.size())
		return -1;

	if (starting_pos >= s)
		return 0;

	if (n > s-starting_pos)
		n=s-starting_pos;

	cb_wrapper<void (size_t, size_t)> cb{lambda};
	unicode_bidi_logical_order(&string[starting_pos],
				   &levels[starting_pos], n,
				   paragraph_embedding,
				   &reorder_callback,
				   reinterpret_cast<void *>(&cb));
	cb.rethrow();
	return 0;
}

void unicode::bidi_logical_order(std::vector<unicode_bidi_level_t> &levels,
				 unicode_bidi_level_t paragraph_embedding,
				 const std::function<void (size_t, size_t)>
				 &lambda,
				 size_t starting_pos,
				 size_t n)
{
	auto s=levels.size();

	if (starting_pos >= s)
		return;

	if (n > s-starting_pos)
		n=s-starting_pos;

	cb_wrapper<void (size_t, size_t)> cb{lambda};
	unicode_bidi_logical_order(NULL, &levels[starting_pos], n,
				   paragraph_embedding,
				   &reorder_callback,
				   reinterpret_cast<void *>(&cb));
	cb.rethrow();
}

extern "C" {
	static void embed_callback(const char32_t *string,
				   size_t n,
				   int is_part_of_string,
				   void *arg)
	{
		auto p=reinterpret_cast<cb_wrapper<void
						   (const char32_t *,
						    size_t n,
						    bool)> *>(arg);
		(*p)(string, n, is_part_of_string != 0);
	}
}

int unicode::bidi_embed(const std::u32string &string,
			const std::vector<unicode_bidi_level_t> &levels,
			unicode_bidi_level_t paragraph_embedding,
			const std::function<void (const char32_t *string,
						  size_t n,
						  bool is_part_of_string)>
			&lambda)
{
	if (string.size() != levels.size())
		return -1;

	if (string.empty())
		return 0;

	cb_wrapper<void (const char32_t *, size_t, bool)> cb{lambda};
	unicode_bidi_embed(&string[0], &levels[0], string.size(),
			   paragraph_embedding,
			   embed_callback,
			   reinterpret_cast<void *>(&cb));

	cb.rethrow();
	return 0;
}

std::u32string unicode::bidi_embed(const std::u32string &string,
				   const std::vector<unicode_bidi_level_t
				   > &levels,
				   unicode_bidi_level_t paragraph_embedding)
{
	std::u32string new_string;

	(void)bidi_embed(string, levels, paragraph_embedding,
			 [&]
			 (const char32_t *string,
			  size_t n,
			  bool ignored)
			 {
				 new_string.insert(new_string.end(),
						   string, string+n);
			 });

	return new_string;
}

char32_t unicode::bidi_embed_paragraph_level(const std::u32string &string,
					     unicode_bidi_level_t level)
{
	return unicode_bidi_embed_paragraph_level(string.c_str(),
						  string.size(),
						  level);
}

unicode_bidi_direction unicode::bidi_get_direction(const std::u32string &string,
						   size_t starting_pos,
						   size_t n)
{
	if (starting_pos >= string.size())
		starting_pos=string.size();

	if (string.size()-starting_pos < n)
		n=string.size()-starting_pos;

	return unicode_bidi_get_direction(string.c_str()+starting_pos, n);
}

bool unicode::bidi_needs_embed(const std::u32string &string,
			       const std::vector<unicode_bidi_level_t> &levels,
			       const unicode_bidi_level_t *paragraph_embedding,
			       size_t starting_pos,
			       size_t n)
{
	if (string.size() != levels.size())
		return false;

	auto s=levels.size();

	if (starting_pos >= s)
		return false;

	if (n > s-starting_pos)
		n=s-starting_pos;

	return unicode_bidi_needs_embed(string.c_str(),
					n == 0 ? NULL : &levels[starting_pos],
					n,
					paragraph_embedding) != 0;
}

std::u32string unicode::bidi_override(const std::u32string &s,
				      unicode_bidi_level_t direction,
				      int cleanup_options)
{
	std::u32string ret;

	ret.reserve(s.size()+1);

	ret.push_back(' ');
	ret.insert(ret.end(), s.begin(), s.end());

	bidi_cleanup(ret, [](size_t) {}, cleanup_options);
	ret.at(0)=direction & 1 ? UNICODE_RLO : UNICODE_LRO;

	return ret;
}

typedef void bidi_combinings_callback_t(unicode_bidi_level_t,
					  size_t level_start,
					  size_t n_chars,
					  size_t comb_start,
					  size_t n_comb_chars);

extern "C" {
	static void bidi_combinings_trampoline(unicode_bidi_level_t level,
					       size_t level_start,
					       size_t n_chars,
					       size_t comb_start,
					       size_t n_comb_chars,
					       void *arg)
	{
		(*reinterpret_cast<cb_wrapper<bidi_combinings_callback_t> *>
		 (arg))(level, level_start, n_chars, comb_start, n_comb_chars);
	}
};

void unicode::bidi_combinings(const std::u32string &string,
			      const std::vector<unicode_bidi_level_t> &levels,
			      const std::function<void (unicode_bidi_level_t,
							size_t level_start,
							size_t n_chars,
							size_t comb_start,
							size_t n_comb_chars)>
			      &callback)
{
	if (string.size() != levels.size() || string.empty())
		return;

	cb_wrapper<bidi_combinings_callback_t> cb{callback};

	unicode_bidi_combinings(&string[0], &levels[0],
				  string.size(),
				  bidi_combinings_trampoline,
				  &cb);
	cb.rethrow();
}

void unicode::bidi_combinings(const std::u32string &string,
			      const std::function<void (unicode_bidi_level_t,
							size_t level_start,
							size_t n_chars,
							size_t comb_start,
							size_t n_comb_chars)>
			      &callback)
{
	if (string.empty())
		return;

	cb_wrapper<bidi_combinings_callback_t> cb{callback};

	unicode_bidi_combinings(&string[0], nullptr,
				  string.size(),
				  bidi_combinings_trampoline,
				  &cb);
	cb.rethrow();
}

void unicode::decompose_default_reallocate(std::u32string &s,
					   const std::vector<std::tuple<size_t,
					   size_t>> &v)
{
	size_t i=0;

	for (auto &t:v)
		i += std::get<1>(t);

	s.resize(s.size()+i);
}

namespace {

	struct decompose_info {
		std::u32string &s;
		const std::function<void (std::u32string &,
					  const std::vector<std::tuple<size_t,
					  size_t>>)> &resizes;
		std::exception_ptr caught;

		void do_reallocate(unicode_decomposition_t *info,
				   const size_t *offsets,
				   const size_t *sizes,
				   size_t n)
		{
			std::vector<std::tuple<size_t, size_t>> v;

			v.reserve(n);

			for (size_t i=0; i<n; ++i)
				v.push_back(std::tuple<size_t,
					    size_t>{offsets[i],
						    sizes[i]});

			resizes(s, v);

			info->string=&s[0];
		}
	};
};

extern "C" {

	static int decompose_reallocate(unicode_decomposition_t *info,
					const size_t *offsets,
					const size_t *sizes,
					size_t n)
	{
		decompose_info *ptr=
			reinterpret_cast<decompose_info *>(info->arg);

		try {
			ptr->do_reallocate(info, offsets, sizes, n);
		} catch (...) {
			ptr->caught=std::current_exception();
			return -1;
		}

		return 0;
	}
}

void unicode::decompose(std::u32string &s,
			int decompose_flags,
			const std::function<void (std::u32string &s,
						  const std::vector<
						  std::tuple<size_t,
						  size_t>>)> &resizes)
{
	if (s.empty())
		return;

	decompose_info info={s, resizes};

	unicode_decomposition_t uinfo;

	unicode_decomposition_init(&uinfo, &s[0], s.size(), &info);
	uinfo.decompose_flags=decompose_flags;
	uinfo.reallocate=decompose_reallocate;
	int rc=unicode_decompose(&uinfo);
	unicode_decomposition_deinit(&uinfo);

	if (info.caught)
		std::rethrow_exception(info.caught);

	if (rc)
		/* unicode_decompose only returns non-0 itself for an enomem */
		throw std::bad_alloc();
}

void unicode::compose_default_callback(unicode_composition_t &)
{
}

namespace {
	struct comps_raii {
		unicode_composition_t comps;

		~comps_raii()
		{
			unicode_composition_deinit(&comps);
		}
	};
};

void unicode::compose(std::u32string &s,
		      int flags,
		      const std::function<void (unicode_composition_t &)> &cb)
{
	if (s.empty())
		return;

	comps_raii comps;

	if (unicode_composition_init(&s[0], s.size(), flags, &comps.comps))
	{
		throw std::bad_alloc(); /* The only reason */
	}

	cb(comps.comps);

	s.resize(unicode_composition_apply(&s[0], s.size(), &comps.comps));
}
