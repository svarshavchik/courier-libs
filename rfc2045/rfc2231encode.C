/*
** Copyright 2025 Double Precision, Inc.  See COPYING for
** distribution information.
*/

/*
*/

#if    HAVE_CONFIG_H
#include "rfc2045_config.h"
#endif
#include	"rfc2045.h"
#include	<errno.h>

int rfc2231_attrCreate(const char *name, const char *value,
		       const char *charset,
		       const char *language,
		       int (*cb_func)(const char *param,
				      const char *value,
				      void *void_arg),
		       void *cb_arg)
{
	return rfc2231_attr_encode(
		name, value,
		charset ? charset:"",
		language ? language:"",
		[=]
		(const char *name, const char *value)
		{
			return cb_func(name, value, cb_arg);
		}
	);
}
