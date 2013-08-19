#ifndef	varlist_h
#define	varlist_h


//
// Quick hack to implement variables - get them and set them.
//

class Buffer;

void UnsetVar(const Buffer &);
void SetVar(const Buffer &, const Buffer &);
const Buffer *GetVar(const Buffer &);
const char *GetVarStr(const Buffer &);
char **ExportEnv();
#endif
