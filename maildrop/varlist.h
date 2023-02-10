#ifndef	varlist_h
#define	varlist_h

#include <string>
#include <vector>

//
// Quick hack to implement variables - get them and set them.
//

void UnsetVar(const std::string &);
void SetVar(const std::string &, const std::string &);
std::string GetVar(const std::string &);
void ExportEnv(std::vector<std::vector<char>> &strings,
	       std::vector<char *> &pts);
#endif
