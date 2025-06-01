#include "hobo_worldcon.h"

#include "hobo_worldcon_irc.h"

const char *HOBO_WorldCon_EnumTypes(int idx)
{
  if (idx==0) return "IRC";
  return NULL;
}

HOBO_WorldCon *HOBO_WorldCon_CreateByType(const char *protocol)
{
  if (!stricmp(protocol,"IRC"))
    return new HOBO_WorldCon_IRC;
  return NULL;
}
