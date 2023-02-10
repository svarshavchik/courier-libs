#ifndef	varlist_h
#define	varlist_h

#include <string>

//
// Quick hack to implement variables - get them and set them.
//

void UnsetVar(const std::string &);
void SetVar(const std::string &, const std::string &);
const std::string *GetVar(const std::string &);
const char *GetVarStr(const std::string &);
char **ExportEnv();
#endif
