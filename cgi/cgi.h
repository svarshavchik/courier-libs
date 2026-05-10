/*
*/
#ifndef	cgi_h
#define	cgi_h

#if	HAVE_CONFIG_H
#include	"cgi/cgi_config.h"
#endif
#include <courier-unicode.h>

#ifdef __cplusplus
#include <string>
#include <vector>
#include <unordered_map>
#include <typeinfo>
#include <iostream>

namespace cgi_encode {

	extern const char default_encode[],
		noamp[], noeq[];

	struct sink {
		virtual void operator()(char)=0;
	};

	void algorithm(sink &, std::string_view, const char *);

	size_t estimate(std::string_view s, const char *str=default_encode);

	template<typename iter> struct sink_cplusplus : sink {
		iter &i;
		sink_cplusplus(iter &i) : i{i} {}
		virtual void operator()(char c) override
		{
			*i++=c;
		}
	};

	template<typename out_iter>
	auto encode(out_iter &&closure,
		    std::string_view s,
		    const char *punct=default_encode)
	{
		sink_cplusplus<out_iter> iter{closure};

		algorithm(iter, s, punct);

		if constexpr (!std::is_same_v<out_iter, out_iter &>)
		{
			return closure;
		}
	}
};

std::vector<std::string> cgi_multiple(const char *);
std::string cgi_cookie(std::string_view);

// Parsed CGI arguments, we store multiple values
// in case of duplicate names (e.g. <input type=checkbox name=test value=1>
// <input type=checkbox name=test value=2>). We use an explicit vector because
// we preserve the order of arguments as they appear in the query string,
// while std::unordered_multimap does not guarantee any order.

extern std::unordered_map<std::string, std::vector<std::string>> cgi_arglist;
extern void cgi_put(std::string_view, std::string_view);
extern void cgi_setup();
extern void cgi_cleanup();

extern "C" {

#endif

#include <string.h>

/*
** Copyright 1998 - 2007 S. Varshavchik.
** See COPYING for distribution information.
*/

extern void fake_exit(int);

const char *cgi(const char *);

void	cgi_setcookie(const char *, const char *);

int	cgi_useragent(const char *);

extern size_t cgiurldecode(char *);

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

int cgi_getfiles( int (*)(const char *, const char *, void *),
		int (*)(const char *, size_t, void *),
		void (*)(void *), size_t, void *);

extern const char *cgihttpscriptptr();
extern const char *cgihttpsscriptptr();
extern const char *cgiextrapath();

extern void cgihttpscriptptr_init();
extern void cgihttpsscriptptr_init();

extern const char *cgirelscriptptr();
extern void cginocache();
extern void cgiredirect(const char *);
extern void cgiversion(unsigned *, unsigned *);
extern int cgihasversion(unsigned, unsigned);

struct cgi_set_cookie_info {
	const char *name;
	const char *value;

	char *domain;
	char *path;
	int age;
	int secure;
};

#define cgi_set_cookie_info_init(i) (memset((i), 0, sizeof(*(i))), (i)->age=-1)
#define cgi_set_cookie_info_free(i) do { if ((i)->path) \
			free((i)->path);		\
		if ((i)->domain)			\
			free((i)->domain);		\
	} while(0)

#define cgi_set_cookie_session(c,n,v) ( ((c)->name=(n)), ((c)->value)=(v))
#define cgi_set_cookie_expired(c,n) ( ((c)->name=(n)), ((c)->value)="",\
				      (c)->age=0)

extern int cgi_set_cookie_url(struct cgi_set_cookie_info *i,
			      const char *url);

#define cgi_set_cookie_secure(c) ((c)->secure=1)

extern void cgi_set_cookies(struct cgi_set_cookie_info *cookies,
			    size_t n_cookies);

extern char *cgi_get_cookie(const char *cookie_name);

extern char *cgi_select(const char *name,
			const char *optvalues,
			const char *optlabels,
			const char *default_value,
			size_t list_size,
			const char *flags); /* "m" - multiple, "d" -disabled */
extern char *cgi_checkbox(const char *name,
			  const char *value,
			  const char *flags);
extern char *cgi_input(const char *name,
		       const char32_t *value,
		       int size,
		       int maxlength,
		       const char *opts);

extern char *cgi_textarea(const char *name,
			  int rows,
			  int cols,
			  const char32_t *value,
			  const char *wrap,
			  const char *opts);

extern void cgiformdatatempdir(const char *);
		/* Specify directory for formdata temp file */

extern void cgi_daemon(int nprocs, const char *lockfile,
		       void (*postinit)(void *),
		       void (*handler)(void *),
		       void *dummy);
extern void cgi_connectdaemon(const char *sockfilename, int pass_fd);

#define SOCKENVIRONLEN 8192

#define VALIDCGIVAR(p) \
		    (strncmp((p), "DOCUMENT_", 9) == 0 || \
		     strncmp((p), "GATEWAY_", 8) == 0 || \
		     strncmp((p), "HTTP_", 5) == 0 || \
		     strncmp((p), "HTTPS=", 6) == 0 || \
		     strncmp((p), "SSL_", 4) == 0 || \
		     strncmp((p), "QUERY_STRING=", 13) == 0 || \
		     strncmp((p), "SQWEBMAIL_", 10) == 0 || \
		     strncmp((p), "REMOTE_", 7) == 0 || \
		     strncmp((p), "REQUEST_", 8) == 0 || \
		     strncmp((p), "SCRIPT_", 7) == 0 || \
		     strncmp((p), "SERVER_", 7) == 0 || \
		     strncmp((p), "CONTENT_", 8) == 0 || \
		     strncmp((p), "PATH_INFO=", 10) == 0)

#define CGI_PASSFD 0

#if CGI_PASSFD_MSGACCRIGHTS
#undef CGI_PASSFD
#define CGI_PASSFD 1
#endif

#if CGI_PASSFD_MSGCONTROL
#undef CGI_PASSFD
#define CGI_PASSFD 1
#endif

#ifdef __cplusplus

}

#endif

#endif
