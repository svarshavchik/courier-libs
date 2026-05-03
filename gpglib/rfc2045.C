/*
** Copyright 2001-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/


#include	"config.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<signal.h>
#include	<errno.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/time.h>

#include	"gpg.h"
#include	"gpglib.h"

#include	"rfc2045/rfc2045.h"
#include	<charconv>

const rfc2045::entity *rfc2045::entity::is_multipart_signed() const
{
	return content_type.value == "multipart/signed"
		&&
		subentities.size() >= 2
		&&
		subentities[1].content_type.value == "application/pgp-signature"
		? &subentities[0]:nullptr;
}

const rfc2045::entity *rfc2045::entity::is_multipart_encrypted() const
{
	return content_type.value == "multipart/encrypted"
		&&
		subentities.size() >= 2
		&&
		subentities[0].content_type.value == "application/pgp-encrypted"
		&&
		subentities[1].content_type.value == "application/octet-stream"
		? &subentities[1]:nullptr;
}

bool rfc2045::entity::has_mimegpg() const
{
	if (is_multipart_signed() || is_multipart_encrypted())
		return true;

	for (auto &e:subentities)
	{
		if (e.has_mimegpg())
			return true;
	}
	return false;
}

std::optional<int> rfc2045::entity::is_decoded() const
{
	if (content_type.value == "multipart/x-mimegpg")
	{
		auto iter=content_type.parameters.find("xpgpstatus");

		if (iter != content_type.parameters.end())
		{
			std::string_view number{iter->second.value};

			int ret=0;

			if (std::from_chars(
				    number.data(),
				    number.data()+number.size(),
				    ret).ec == std::error_code{})
				return ret;
		}
	}
	return {};
}

const rfc2045::entity *rfc2045::entity::decoded_content() const
{
	if (subentities.size() >= 2)
		return &subentities[1];
	return nullptr;
}


const rfc2045::entity *rfc2045::entity::signed_content() const
{
	if (!subentities.empty())
		return &subentities[0];
	return nullptr;
}
