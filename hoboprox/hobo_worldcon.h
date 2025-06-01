#ifndef _HOBO_WORLDCON_H_
#define _HOBO_WORLDCON_H_

#include <stdlib.h>
#include <string.h>

#include "../WDL/queue.h"
#include "../WDL/wdlstring.h"
#include "../WDL/jnetlib/jnetlib.h"


class HOBO_WorldCon // virtual base class for different world-communication protocols
{
public:

  HOBO_WorldCon() { }
  virtual ~HOBO_WorldCon() { };

  virtual const char *GetProtocol()=0; // "AIM", "IRC", etc. no spaces.
  virtual const char *GetUniqueString()=0; // "AIM:username", or "IRC:server:port", etc. no spaces.

  virtual const char *GetNick()=0;

  virtual const char *GetConnectString()=0;

  virtual bool IsConnected()=0;
  
  virtual const char *Connect(const char *dest)=0;  // returns help string if invalid


  // send a IRC-message to the host (the WorldCon should convert to native if necessary)
  virtual bool SendMsg(const char *fmt, ...)=0; // true on success

  // runs processing (call often)
  virtual void Run(void (*MessageCallback)(const char *msg, void *userData), void *userData)=0;
};


const char *HOBO_WorldCon_EnumTypes(int idx);
HOBO_WorldCon *HOBO_WorldCon_CreateByType(const char *protocol);

#endif//_HOBO_WORLDCON_H_
