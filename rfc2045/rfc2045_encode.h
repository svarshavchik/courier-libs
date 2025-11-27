#ifdef rfc2045_rfc2045_h_included
#ifdef rfc2045_encode_h_included

template<bool crlf>
template<typename out_iter, typename src_type>
void rfc2045::entity::line_iter<crlf>
::autoconvert_entity(const entity &e, out_iter &closure, src_type &src,
		     autoconvert_meta &metadata)
{
	char buffer[BUFSIZ];

	std::function<void (const char *, size_t)> reencoder;

	std::optional<rfc822::encode<out_iter &>> encoder;

	bool preserve_transfer_encoding=false;

	auto rewrite_transfer_encoding=e.rewrite_transfer_encoding;

	if (!e.subentities.empty())
		preserve_transfer_encoding=true;
	else switch (e.rewrite_transfer_encoding) {
	case cte::error:
		preserve_transfer_encoding=true;
		rewrite_transfer_encoding=e.content_transfer_encoding;
		break;
	case cte::sevenbit:
	case cte::eightbit:
		reencoder=[&]
			(const char *ptr, size_t n)
		{
			if (ptr)
				closure(ptr, n);
		};
		break;
	case cte::qp:
	case cte::base64:   // autoconvert_check never does this

		reencoder=[encoder=std::ref(
				encoder.emplace(
					closure,
					e.rewrite_transfer_encoding == cte::qp ?
					"quoted-printable":"base64"))]
			(const char *ptr, size_t n)
		{
			if (!ptr)
			{
				encoder.get().end();
				return;
			}

			encoder(ptr, n);
		};
		break;
	}

	headers existing_headers{e, src};
	existing_headers.name_lc=false;
	existing_headers.keep_eol=true;

	std::string content_type_header="Content-Type: " +
		e.content_type.value;

	if (std::string_view{e.content_type.value}.substr(0, 5) == "text/")
	{
		content_type_header += "; charset=";
		content_type_header += e.content_type_charset();
	}
	content_type_header += eol;

	std::string content_transfer_encoding_header;

	content_transfer_encoding_header.reserve(80);

	content_transfer_encoding_header =
		"Content-Transfer-Encoding: ";
	content_transfer_encoding_header += rfc2045::to_cte(
		rewrite_transfer_encoding
	);
	content_transfer_encoding_header += eol;

	bool mime1=e.mime1;

	std::string_view mime1_0{"Mime-Version: 1.0"};

	do
	{
		auto [name_lc, this_line_is_empty] =
			existing_headers.convert_name_check_empty();

		if (!e.subentities.empty())
		{
			// We're here to pass through a multipart entity, as is

			name_lc="";
		}

		if (name_lc == "content-type")
		{
			if (!mime1)
			{
				closure(mime1_0.data(), mime1_0.size());
				closure(eol.data(), eol.size());
				mime1=true;
			}
			if (!content_type_header.empty())
				closure(content_type_header.data(),
					content_type_header.size());
			content_type_header.clear();
			continue;
		}

		if (name_lc == "content-transfer-encoding")
		{
			if (!mime1)
			{
				closure(mime1_0.data(), mime1_0.size());
				closure(eol.data(), eol.size());
				mime1=true;
			}
			if (!content_transfer_encoding_header.empty())
				closure(content_transfer_encoding_header.data(),
					content_transfer_encoding_header.size()
				);
			content_transfer_encoding_header.clear();
			continue;
		}

		if (!this_line_is_empty)
		{
			auto current_header=existing_headers.current_header();
			current_header=metadata.rwheader(
				e,
				name_lc,
				current_header
			);
			closure(current_header.data(), current_header.size());
		}
	} while (existing_headers.next());

	if (e.subentities.empty())
		// Otherwise we've preserved a multipart entity as is
	{
		if (!mime1)
		{
			closure(mime1_0.data(), mime1_0.size());
			closure(eol.data(), eol.size());
		}
		if (!content_type_header.empty())
			closure(content_type_header.data(),
				content_type_header.size());
		if (!content_transfer_encoding_header.empty())
			closure(content_transfer_encoding_header.data(),
				content_transfer_encoding_header.size()
			);
	}

	if (preserve_transfer_encoding)
	{
		closure(eol.data(), eol.size());

		// Don't bother to decode and reencode. Copy verbatim.
		src.pubseekpos(e.startbody);

		auto n=e.endbody-e.startbody;

		while (n)
		{
			auto chunk=n;

			if (chunk > BUFSIZ) chunk=BUFSIZ;

			auto done=src.sgetn(buffer, chunk);

			if (done == 0)
				return; // Must be an error;

			closure(buffer, done);
			n -= done;
		}

		return;
	}

	closure("X-Mime-Autoconverted: from ", 27);

	std::string_view cte_name{
		rfc2045::to_cte(e.content_transfer_encoding)
	};

	closure(cte_name.data(), cte_name.size());

	closure(" to ", 4);

	cte_name=rfc2045::to_cte(e.rewrite_transfer_encoding);
	closure(cte_name.data(), cte_name.size());
	if (metadata.appid.size())
	{
		closure(" by ", 4);
		closure(metadata.appid.data(), metadata.appid.size());
	}

	closure(eol.data(), eol.size());
	closure(eol.data(), eol.size());

	rfc822::mime_decoder do_decoder{reencoder, src};

	do_decoder.decode_header=false;
	do_decoder.decode(e);
	reencoder(nullptr, 0);
}

#endif
#endif
