#include "config.h"
/*
** Copyright 2000-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

/*
*/

#include	"sqwebmail.h"
#include	"addressbook.h"
#include	"maildir.h"
#include	"cgi/cgi.h"
#include	"rfc822/rfc822.h"
#include	"rfc2045/rfc2045.h"
#include	"maildir/maildirmisc.h"
#include	"numlib/numlib.h"
#include	<stdio.h>
#include	<string.h>
#include	<ctype.h>
#include	<fcntl.h>
#include	<set>
#include	<map>
#include	<algorithm>
#include	<string>
#include	<vector>
#include	<iterator>
#include	<string_view>
#define	ADDRESSBOOK	"sqwebmail-addressbook"

extern const char *sqwebmail_content_charset;

void output_attrencoded(const char *);
void output_attrencoded_fp(const char *, FILE *);
void output_attrencoded_fplen(const char *, size_t, FILE *);
extern void print_safe(const char *);
extern void call_print_safe_to_stdout(const char *p, size_t cnt);

static std::string q_escape(std::string_view name)
{
	std::string result;

	for (char c : name)
	{
		if (iscntrl((int)(unsigned char)c))
			continue;
		if (c == '"' || c == '\\')
			result += '\\';
		result += c;
	}
	return result;
}

/*
** When adding a new name/address pair into the address book delete
** bad characters from both.
*/

static void fix_nameaddr(
	std::string &name, std::string &addr
)
{
	name=q_escape(name);

	auto p=addr.begin();
	auto q=p;

	for (; p != addr.end(); ++p)
	{
		if (iscntrl((int)(unsigned char)*p))
			continue;
		if (isspace((int)(unsigned char)*p))
			continue;
		if (*p == '<' || *p == '>' || *p == '(' || *p == ')' ||
			*p == '\\' || *p == '"')
		{
			continue;
		}
		*q++=*p;
	}
	addr.resize(q-addr.begin());
}

static void edit_nick(std::string &nick)
{
	auto p=nick.begin(), q=p;
	for (; p != nick.end(); ++p)
	{
		if (isspace((int)(unsigned char)*p))	continue;
		if (iscntrl((int)(unsigned char)*p))	continue;
		if (strchr(":;,<>@\\", *p))	continue;
		*q++=*p;
	}
	nick.resize(q-nick.begin());
}

void ab_add(const char *name, const char *address, const char *nick)
{
	if (*nick == 0 || *address == 0)
		return;

	auto nick_utf8=unicode::iconvert::convert(
		nick,
		sqwebmail_content_charset,
		unicode::utf_8
	);
	auto name_utf8=unicode::iconvert::convert(
		name,
		sqwebmail_content_charset,
		unicode::utf_8
	);
	auto address_utf8=unicode::iconvert::convert(
		address,
		sqwebmail_content_charset,
		unicode::utf_8
	);

	/* Delete bad characters from nickname, name, address */

	edit_nick(nick_utf8);

	if (nick_utf8.empty())
		return;

	/* Remove quotes from name */

	fix_nameaddr(name_utf8, address_utf8);

	if (address_utf8.empty())
		return;

	rfc822::fdstreambuf fp{
		open(ADDRESSBOOK, O_RDONLY)
	};

	std::string new_name;

	rfc822::fdstreambuf new_fpbuf{
		maildir_createmsg(INBOX, "addressbook", new_name)
	};

	new_name="tmp/" + new_name;

	if (new_fpbuf.error())
	{
		enomem();
		return;
	}

	bool written=false;
	rfc2045::entity::line_iter<false>::headers headers{fp};

	headers.name_lc=true;
	headers.keep_eol=true;

	do
	{
		const auto &[header, value]=headers.name_content();

		if (header.empty())
			break;

		if (header == nick_utf8)
		{
			std::string_view value_cpy=value;

			if (!value_cpy.empty() && value_cpy.back() == '\n')
				value_cpy.remove_suffix(1);
			new_fpbuf.sputn(nick_utf8.data(), nick_utf8.size());
			new_fpbuf.sputn(": ", 2);
			new_fpbuf.sputn(value_cpy.data(), value_cpy.size());
			new_fpbuf.sputn(",\n    ", 5);
			written=true;
			headers.next();
			break;
		}
		new_fpbuf.sputn(header.data(), header.size());
		new_fpbuf.sputn(": ", 2);
		new_fpbuf.sputn(value.data(), value.size());
	} while (headers.next());
	if (!written)
	{
		new_fpbuf.sputn(nick_utf8.data(), nick_utf8.size());
		new_fpbuf.sputn(": ", 2);
	}
	if (!name_utf8.empty())
	{
		new_fpbuf.sputn("\"", 1);
		new_fpbuf.sputn(name_utf8.data(), name_utf8.size());
		new_fpbuf.sputn("\" ", 2);
		new_fpbuf.sputn("<", 1);
		new_fpbuf.sputn(address_utf8.data(), address_utf8.size());
		new_fpbuf.sputn(">\n", 2);
	}
	else
	{
		new_fpbuf.sputn("<", 1);
		new_fpbuf.sputn(address_utf8.data(), address_utf8.size());
		new_fpbuf.sputn(">\n", 2);
	}

	do
	{
		const auto &[header, value]=headers.name_content();

		if (header.empty())
			break;

		new_fpbuf.sputn(header.data(), header.size());
		new_fpbuf.sputn(": ", 2);
		new_fpbuf.sputn(value.data(), value.size());
	} while (headers.next());

	new_fpbuf.pubsync();
	if (new_fpbuf.error())
	{
		unlink(new_name.c_str());
		error("Unable to write out new address book -- write error, or out of disk space.");
		return;
	}
	new_fpbuf={};
	rename(new_name.c_str(), ADDRESSBOOK);
}

/* note: we're always passing utf-8 to dodel() */

static void dodel(
	std::string_view nick,
	rfc822::addresses &a,
	size_t n,
	std::string replace_name,
	std::string replace_addr
)
{
	if (n >= a.size())
		return;

	if (!replace_addr.empty())
	{
		fix_nameaddr(replace_name, replace_addr);
		a[n].name.clear();
		if (!replace_name.empty())
		{
			rfc822::token t;
			t.type='"';
			t.str=replace_name;
			a[n].name.push_back(t);
		}
		a[n].address.clear();
		rfc822::token t;
		t.str=replace_addr;
		a[n].address.push_back(t);
	}
	else
	{
		a.erase(a.begin()+n);
	}

	rfc822::fdstreambuf fp{
		open(ADDRESSBOOK, O_RDONLY)
	};

	std::string new_name;

	rfc822::fdstreambuf new_fpbuf{
		maildir_createmsg(INBOX, "addressbook", new_name)
	};
	if (new_fpbuf.error())
	{
		enomem();
		return;
	}

	std::ostream new_fp{&new_fpbuf};

	new_name="tmp/" + new_name;

	rfc2045::entity::line_iter<false>::headers headers{fp};

	headers.name_lc=true;
	headers.keep_eol=true;

	do
	{
		const auto &[header, value]=headers.name_content();

		if (header.empty())
			break;

		if (header == nick)
		{
			if (a.empty())
				continue;

			std::vector<std::string> s;

			s.reserve(a.print_wrapped(
				70, rfc822::length_counter{}
			));

			a.print_wrapped(70, std::back_inserter(s));

			new_fp.write(header.data(), header.size());
			new_fp.write(": ", 2);
			const char *pfix="";
			for (const auto &s : s)
			{
				new_fp.write(pfix, strlen(pfix));
				new_fp.write(s.data(), s.size());
				pfix="\n  ";
			}
			new_fp.write("\n", 1);
			continue;
		}
		new_fp.write(header.data(), header.size());
		new_fp.write(": ", 2);
		new_fp.write(value.data(), value.size());
	} while (headers.next());

	new_fpbuf.pubsync();
	if (new_fpbuf.error())
	{
		enomem();
		return;
	}
	new_fpbuf=rfc822::fdstreambuf{};
	rename(new_name.c_str(), ADDRESSBOOK);
}

static void dodelall(std::string_view nick)
{
	rfc822::fdstreambuf fp{open(ADDRESSBOOK, O_RDONLY)};

	std::string new_name;

	rfc822::fdstreambuf new_fpbuf{
		maildir_createmsg(INBOX, "addressbook", new_name)
	};
	if (new_fpbuf.error())
	{
		enomem();
		return;
	}
	new_name="tmp/" + new_name;

	rfc2045::entity::line_iter<false>::headers headers{fp};

	headers.name_lc=true;
	headers.keep_eol=true;

	do
	{
		const auto &[header, value]=headers.name_content();

		if (header.empty())
			break;

		if (header == nick)
			continue;
		new_fpbuf.sputn(header.data(), header.size());
		new_fpbuf.sputn(": ", 2);
		new_fpbuf.sputn(value.data(), value.size());
	} while (headers.next());

	new_fpbuf.pubsync();
	if (new_fpbuf.error())
	{
		unlink(new_name.c_str());
		error("Unable to write out new address book -- write error, or out of disk space.");
		return;
	}

	new_fpbuf={};
	rename(new_name.c_str(), ADDRESSBOOK);
}

void ab_listselect()
{
	ab_listselect_fp(stdout);
}

void ab_listselect_fp(FILE *w)
{
	rfc822::fdstreambuf fp{
		open(ADDRESSBOOK, O_RDONLY)
	};

	if (fp.error())
		return;

	std::vector<std::string> names;
	rfc2045::entity::line_iter<false>::headers headers{fp};

	headers.name_lc=true;
	headers.keep_eol=true;

	do
	{
		const auto &[header, value]=headers.name_content();

		if (header.empty())
			break;

		names.push_back({header.begin(), header.end()});
	} while (headers.next());

	std::sort(names.begin(), names.end());

	for (const auto &name : names)
	{
		bool errflag;

		auto p=unicode::iconvert::convert(
			name,
			unicode::utf_8,
			sqwebmail_content_charset,
			errflag
		);

		fprintf(w, "<option value=\"");
		output_attrencoded_fplen(name.data(), name.size(), w);
		fprintf(w, "\">");

		output_attrencoded_fplen(p.data(), p.size(), w);
		fprintf(w, "</option>\n");
	}
}

/*
** Extract all name/address entries from the address book, for external
** processing (mostly calendaring).
*/

int ab_get_nameaddr( int (*callback_func)(const char *, const char *,
					  void *),
		     void *callback_arg)
{
	int rc=0;

	rfc822::fdstreambuf fp{
		open(ADDRESSBOOK, O_RDONLY)
	};

	if (fp.error())
		return (0);

	rfc2045::entity::line_iter<false>::headers headers{fp};

	headers.name_lc=true;
	headers.keep_eol=true;

	do
	{
		const auto &[header, value]=headers.name_content();

		if (header.empty())
			break;

		if (value.empty())
			continue;

		rfc822::tokens t{value};
		rfc822::addresses a{t};

		for (const auto &addr : a)
		{
			if (addr.address.empty())
				continue;

			std::string addr_s;
			std::string name_s;

			addr_s.reserve(
				addr.address.unquote(rfc822::length_counter{})
			);
			addr.address.unquote(std::back_inserter(addr_s));

			name_s.reserve(
				addr.name.unquote(rfc822::length_counter{})
			);
			addr.name.unquote(std::back_inserter(name_s));

			rc=(*callback_func)(
				addr_s.c_str(),
				name_s.c_str(),
				callback_arg
			);

			if (rc)
				break;
		}

		if (rc)
			break;
	} while (headers.next());

	return (rc);
}

static int ab_addrselect_cb(const char *a, const char *n, void *vp)
{
	auto *p=reinterpret_cast<std::map<std::string, std::set<std::string>> *>(vp);

	if (!n || !a)
		return (0);

	(*p)[n].insert(a);

	return (0);
}

static void ab_show_utf8(const std::string &p)
{
	bool errflag=false;
	std::string p_s=
		unicode::iconvert::convert(
			p,
			unicode::utf_8,
			sqwebmail_content_charset,
			errflag
		);

	if (errflag)
		p_s += " (conversion error)";

	auto ptr=p_s.data();

	while (ptr < p_s.data() + p_s.size())
	{
		size_t i;

		for (i=0; ptr+i < p_s.data() + p_s.size(); ++i)
		{
			if (ptr[i] == '"' || ptr[i] == '\\')
				break;
		}

		if (i)
			call_print_safe_to_stdout(ptr, i);

		ptr += i;

		if (ptr < p_s.data() + p_s.size())
		{
			char buf[NUMBUFSIZE+10]="&#";

			strcpy(std::to_chars(buf+2, buf+sizeof(buf)-8,
						static_cast<unsigned char>(*ptr)
					).ptr, ";");
			printf("%s", buf);
			++ptr;
		}
	}
	return;
}


void ab_nameaddr_show(const std::string &name, const std::string &addr)
{
	if (!name.empty())
	{
		printf("\"");
		ab_show_utf8(name);
		printf("\"&nbsp;");
	}
	printf("&lt;");

	if (!addr.empty())
		ab_show_utf8(addr);

	printf("&gt;");
}

void ab_addrselect()
{
	std::map<std::string, std::set<std::string>> m;

	printf("<select name=\"addressbookname\"><option value=\"\"></option>\n");

	if (ab_get_nameaddr(ab_addrselect_cb, &m) == 0)
	{
		for (const auto &[name, addrs] : m)
		{
			for (const auto &addr : addrs)
			{
				printf("<option value=\"");
				output_attrencoded(addr.c_str());
				printf("\">");
				ab_nameaddr_show(name, addr);
				printf("</option>\n");
			}
		}
	}
	printf("</select>\n");
}

std::string ab_find(std::string_view nick)
{
	rfc822::fdstreambuf fp(open(ADDRESSBOOK, O_RDONLY));

	if (fp.error())
		return {};

	rfc2045::entity::line_iter<false>::headers headers{fp};

	headers.name_lc=true;
	headers.keep_eol=true;

	do
	{
		const auto &[header, value]=headers.name_content();

		if (header.empty())
			break;

		if (header == nick)
			return std::string{value.begin(), value.end()};
	}
	while (headers.next());

	return {};
}
void addressbook()
{
	const char	*nick_prompt=getarg("PROMPT");
	const char	*nick_submit=getarg("SUBMIT");
	const char	*nick_title1=getarg("TITLE1");
	const char	*nick_title2=getarg("TITLE2");
	const char	*nick_delete=getarg("DELETE");
	const char	*nick_name=getarg("NAME");
	const char	*nick_address=getarg("ADDRESS");
	const char	*nick_add=getarg("ADD");
	const char	*nick_edit=getarg("EDIT");
	const char	*nick_select1=getarg("SELECT1");
	const char	*nick_select2=getarg("SELECT2");
	const char *nick1;
	bool	do_edit=false;

	std::string edit_name;
	std::string edit_addr;
	size_t	replace_index=0;

#if 0
	fp=fopen("/tmp/pid", "w");
	fprintf(fp, "%d", getpid());
	fclose(fp);
	sleep(10);
#endif

	nick1=cgi("nick");

	if (*cgi("nick.edit"))
		do_edit=true;
	else if (*cgi("nick.edit2"))
	{
		do_edit=true;
		nick1=cgi("nick2");
	}
	else if (*cgi("editnick"))
	{
		do_edit=true;
		nick1=cgi("editnick");
	}

	if (*cgi("ADDYCNT"))	/* Import from LDAP */
	{
	unsigned counter=atoi(cgi("ADDYCNT"));
	char	numbuf[NUMBUFSIZE];
	char	numbuf2[NUMBUFSIZE+10];
	unsigned	i;

		if (counter < 1 || counter > 1000)
			counter=1000;
		nick1=cgi("nick2");
		if (!*nick1)
			nick1=cgi("nick1");

		if (*nick1)
		{
			do_edit=true;
			for (i=0; i<counter; i++)
			{
			const char *addy=cgi(strcat(strcpy(numbuf2, "ADDY"),
                                        libmail_str_size_t(i, numbuf)));
			char	*addycpy;
			char	*name;

				if (*addy == 0)	continue;

				addycpy=strdup(addy);
				if (!addycpy)	enomem();

				name=strchr(addycpy, '>');
				if (!name)
				{
					free(addycpy);
					continue;
				}
				*name++=0;
				while (*name == ' ')	++name;
				addy=addycpy;
				if (*addy == '<')	++addy;
				ab_add(name, addy, nick1);
			}
		}
	}

	if (*cgi("nick.delete"))
	{
		std::string p=
			unicode::iconvert::convert(
				cgi("nick"),
				sqwebmail_content_charset,
				unicode::utf_8
			);

		do_edit=false;

		if (!p.empty())
			dodelall(p);
	}
	else if (*cgi("add"))
	{
	const char *newname=cgi("newname");
	const char *newaddr=cgi("newaddress");
	const char *editnick=cgi("editnick");
	const char *replacenum=cgi("replacenick");

		if (*replacenum)
		{
			auto editnick_utf8=
				unicode::iconvert::convert(
					editnick,
					sqwebmail_content_charset,
					unicode::utf_8
				);

			std::string value=ab_find(editnick_utf8);
			rfc822::tokens t{value};
			rfc822::addresses a{t};

			dodel(
				editnick_utf8,
				a,
				atoi(replacenum),
				unicode::iconvert::convert(
					newname,
					sqwebmail_content_charset,
					unicode::utf_8
				),
				unicode::iconvert::convert(
					newaddr,
					sqwebmail_content_charset,
					unicode::utf_8
				)
			);
		}
		else
			ab_add(newname, newaddr, editnick);
		do_edit=true;
		nick1=editnick;
	}

	printf("%s", nick_prompt);
	printf("%s\n", nick_select1);

	ab_listselect();

	printf("%s\n", nick_select2);
	printf("%s", nick_submit);

	std::string s=nick1;
	edit_nick(s);

	if (do_edit && !s.empty())
	{
		printf("<input type=\"hidden\" name=\"editnick\" value=\"");
		output_attrencoded_fplen(s.data(), s.size(), stdout);
		printf("\" />\n");

		printf("<table border=\"0\" class=\"nickedit-box\">\n");
		printf("<tr><td colspan=\"3\">\n");

		printf("%s", nick_title1);
		output_attrencoded_fplen(s.data(), s.size(), stdout);
		printf("%s\n", nick_title2);

		auto s_utf8=
			unicode::iconvert::convert(
				s,
				sqwebmail_content_charset,
				unicode::utf_8
			);
		std::string value=ab_find(s_utf8);

		if (!value.empty())
		{
			rfc822::tokens t{value};
			rfc822::addresses a{t};

			for (size_t i=0; i<a.size(); ++i)
			{
				char buf[NUMBUFSIZE+10]="del";

				*std::to_chars(
					buf+3,
					buf+sizeof(buf)-1,
					i
				).ptr=0;
				if (*cgi(buf))
				{
					dodel(s, a, i, "", "");
					break;
				}
				strcpy(buf, "startedit");
				*std::to_chars(
					buf+9,
					buf+sizeof(buf)-1,
					i
				).ptr=0;
				if (*cgi(buf))
				{
					edit_name.clear();
					edit_name.reserve(
						a[i].name.unquote(
							rfc822::length_counter{}
						)
					);
					a[i].name.unquote(
						std::back_inserter(edit_name)
					);
					edit_addr.clear();
					edit_addr.reserve(
						a[i].address.unquote(
							rfc822::length_counter{}
						)
					);
					a[i].address.unquote(
						std::back_inserter(edit_addr)
					);
					replace_index=i;
					break;
				}
			}

			for (size_t i=0; i<a.size(); ++i)
			{
				if (a[i].address.empty())
					continue;
				printf("<tr><td align=\"right\""
					" class=\"nickname\">");

				if (!a[i].name.empty())
					/* getname defaults it
					** here.
					*/
				{
					std::string n;

					n.reserve(
						a[i].name.unquote(
							rfc822::length_counter{}
					));

					a[i].name.unquote(
						std::back_inserter(n)
					);

					if (!n.empty())
					{
						printf("\"");
						ab_show_utf8(n);
						printf("\"");
					}

				}

				printf("</td><td align=\"left\""
					" class=\"nickaddr\">"
					"&lt;");
				std::string s;

				s.reserve(
					a[i].address.unquote(
						rfc822::length_counter{}
					)
				);
				a[i].address.unquote(
					std::back_inserter(s)
				);

				if (!s.empty())
				{
					ab_show_utf8(s);
				}
				printf("&gt;</td><td><input type=\"submit\" name=\"startedit%d\" value=\"%s\" />&nbsp;<input type=\"submit\" name=\"del%d\" value=\"%s\" /></td></tr>\n",
					static_cast<int>(i), nick_edit,
					static_cast<int>(i), nick_delete);
			}
		}
		printf("<tr><td colspan=\"3\"><hr width=\"90%%\" /></td></tr>\n");
		printf("<tr><td align=\"right\">%s</td><td colspan=\"2\"><input type=\"text\" name=\"newname\" class=\"nicknewname\"", nick_name);

		if (!edit_name.empty())
		{
			bool errflag=false;

			auto edit_name_native=
				unicode::iconvert::convert(
					edit_name,
					unicode::utf_8,
					sqwebmail_content_charset,
					errflag
				);

			if (errflag)
				edit_name_native += " (conversion error)";
			printf(" value=\"");
			output_attrencoded(edit_name_native.c_str());
			printf("\"");
		}
		printf(" /></td></tr>\n");

		printf("<tr><td align=\"right\">%s</td><td><input type=\"text\" name=\"newaddress\" class=\"nicknewaddr\"", nick_address);
		if (!edit_addr.empty())
		{
			bool errflag=false;

			auto edit_addr_native=
				unicode::iconvert::convert(
					edit_addr,
					unicode::utf_8,
					sqwebmail_content_charset,
					errflag
				);

			if (errflag)
				edit_addr_native += " (conversion error)";
			printf(" value=\"");
			output_attrencoded(edit_addr_native.c_str());
			printf("\"");
		}
		printf(" /></td><td>");

		if (!edit_name.empty() || !edit_addr.empty())
			printf("<input type=\"hidden\" name=\"replacenick\" value=\"%d\" />",
				static_cast<int>(replace_index));

		printf("<input type=\"submit\" name=\"add\" value=\"%s\" /></td></tr>\n",
			!edit_name.empty() || !edit_addr.empty() ? nick_edit:nick_add);

		printf("</table>\n");
	}
}
