/*
** Copyright 2025 S. Varshavchik.
** See COPYING for distribution information.
**
*/

#include "rfc2045/rfc2045.h"

static std::string_view dsnaddr(std::string &addr)
{
	auto str=rfc6533::decode(addr);

	bool conv_error{false};
	auto c=unicode::iconvert::convert(
		str,
		unicode::utf_8,
		rfc2045_getdefaultcharset(),
		conv_error
	);

	addr=c;

	return addr;
}

static void print_dsn_recip(std::string origrecip,
			    std::string finalrecip,
			    std::string action,
			    const rfc2045::entity::dsn_callback_t &cb)
{
	rfc2045::entity::tolowercase(action);

	auto ab=action.begin(), ae=action.end();

	while (ab != ae && isspace((unsigned char)*ab))
		++ab;

	while (ab != ae && isspace((unsigned char)ae[-1]))
		++ae;

	if (ab == ae)
		return;


	cb({
			{&*ab, static_cast<size_t>(ae-ab)},
			dsnaddr(origrecip),
			dsnaddr(finalrecip)
		});
}

rfc2045::entity::dsn_handler::dsn_handler(const dsn_callback_t &callback)
	: callback{callback}
{
	// Truncate each line to 1024 characters

	linebuf.reserve(1024);
}

const rfc2045::entity *rfc2045::entity::dsn_handler::report(
	const rfc2045::entity &entity
)
{
	std::string report_type_value;

	if (auto report_type=entity.content_type.parameters.find(
		    "report-type"
	    ); report_type != entity.content_type.parameters.end())
	{
		report_type_value=report_type->second.value;

		tolowercase(report_type_value);
	}

	auto delivery_status=std::find_if(
		entity.subentities.begin(),
		entity.subentities.end(),
		[]
		(auto &se)
		{
			return rfc2045_delivery_status_content_type(
				se.content_type.value.c_str()
			);
		});

	if (entity.content_type.value != "multipart/report" ||
	    report_type_value != "delivery-status" ||
	    delivery_status == entity.subentities.end())
		return nullptr;

	return &*delivery_status;
}

void rfc2045::entity::dsn_handler::operator()(const char *p, size_t n)
{
	while (n)
	{
		auto q=p;
		p=std::find(p, p+n, '\n');
		n -= p-q;

		size_t c=p-q;

		if (c+linebuf.size() > 1024)
			c=1024-linebuf.size();

		linebuf.insert(linebuf.end(), q, q+c);

		if (n && *p == '\n')
		{
			++p;
			--n;

			if (linebuf.empty() ||
			    isspace(linebuf[0]))
			{
				finish();
				origreceip.clear();
				finalreceip.clear();
				action.clear();
			}

			size_t colon=linebuf.find(':');

			if (colon > linebuf.size())
				colon=linebuf.size();

			auto vb=linebuf.begin()+colon;

			tolowercase(linebuf.begin(), vb);

			std::string_view word{
				linebuf.data(),
				static_cast<size_t>(vb-linebuf.begin())
			};

			auto ve=linebuf.end();

			if (vb < ve) ++vb;
			while (vb < ve && isspace(*vb))
				++vb;

			while (vb < ve && isspace(ve[-1]))
				--ve;

			if (word == "action")
				action=std::string{vb, ve};

			if (word == "final-recipient")
				finalreceip=std::string{vb, ve};

			if (word == "original-recipient")
				origreceip=std::string{vb, ve};
			linebuf.clear();
		}
	}
}

void rfc2045::entity::dsn_handler::finish()
{
	print_dsn_recip(
		std::move(origreceip),
		std::move(finalreceip),
		std::move(action),
		callback
	);
}
