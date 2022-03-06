#define RUNTIME_START 100
#define RUNTIME_CUR 200

#define SAVEHOOK() do { if (getenv("POP3DEBUGNOSAVE")) return -1; } while (0)

#include "pop3dserver.c"
