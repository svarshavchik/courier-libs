#ifndef authconfigfile_h
#define authconfigfile_h

#if HAVE_CONFIG_H
#include "courier_auth_config.h"
#endif

#include <time.h>
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <string>
#include <map>

namespace courier {
	namespace auth {
#if 0
	}
}
#endif

class config_file {

protected:
	const char *filename;

	std::map<std::string, std::string> parsed_config;

private:
	bool loaded;
	time_t config_timestamp;

public:
	config_file(const char *filenameArg);
	bool load(bool reload=false);

private:
	virtual bool do_load()=0;
	virtual void do_reload()=0;

	class isspace;
	class not_isspace;

	bool open_and_load_file(bool reload);

 public:

	static std::string expand_string(const std::string &s,
					 const std::map<std::string,
					 std::string> &parameters);

	static std::string
		parse_custom_query(const std::string &s,
				   const std::string &login,
				   const std::string &defdomain,
				   std::map<std::string,
				   std::string> &parameters);
};

#if 0
{
	{
#endif
	}
}

#endif
