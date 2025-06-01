#ifndef _HOBO_WORLDCON_IRC_H_
#define _HOBO_WORLDCON_IRC_H_

#include <stdlib.h>
#include <stdio.h>

#include "hobo_worldcon.h"
#include "../WDL/jnetlib/jnetlib.h"
#include "../WDL/ptrlist.h"

class HOBO_WorldCon_IRC : public HOBO_WorldCon
{
public:
  HOBO_WorldCon_IRC();
  ~HOBO_WorldCon_IRC();

  virtual const char *GetProtocol() { return "IRC"; }; // "AIM", "IRC", etc. no spaces.
  virtual const char *GetUniqueString(); // "AIM:username", or "IRC:server:port", etc. no spaces.
  
  virtual const char *GetNick() { return m_actual_nick.Get(); } 

  virtual const char *GetConnectString() { return m_connect_str.Get(); };

  virtual bool IsConnected();
  
  virtual const char *Connect(const char *dest);  // returns help string if invalid

  virtual bool SendMsg(const char *fmt, ...); // true on success

  virtual void Run(void (*MessageCallback)(const char *msg, void *userData), void *userData);



//////////////


  WDL_String m_servername, m_nickname, m_identname, m_passstr,m_realname,m_connect_str;

  WDL_String m_actual_nick;

  WDL_String m_uniqstr;
  time_t m_lastconnect_time;
  int m_reconnect_interval;

  JNL_IConnection *m_con;

  enum IRCState { NotConnected=-1, 
                  Connecting=0, 
                  Connected,
  };
  IRCState m_state;
};

#endif//_HOBO_WORLDCON_IRC_H_
