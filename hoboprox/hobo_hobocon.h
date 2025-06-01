#ifndef _HOBO_HOBOCON_H_
#define _HOBO_HOBOCON_H_

#include "../WDL/jnetlib/jnetlib.h"
#include "../WDL/wdlstring.h"
#include "../WDL/wdlcstring.h"
#include "../WDL/queue.h"

#include "../WDL/sha.h"
#include "../WDL/wdltypes.h"

class HOBO_Server;

class HOBO_HoboCon // manages an IRC client connection
{
public:
  HOBO_HoboCon(JNL_IConnection *con, const char *nickline, const char *userline, HOBO_Server *parentserver);
  ~HOBO_HoboCon();

  bool Run(void (*MessageCallBack)(const char *msg, void *userData, HOBO_HoboCon *con), void *userData); // return true if DISCONNECTED

  bool SendMsg(const char *fmt, ...); // true on success, false if disconnected or insufficient buffer
  bool TalkToUser(const char *fmt, ...); // true on success, false if disconnected or insufficient buffer

  WDL_String m_realnameident; // used to index various things
  WDL_String m_nickline, m_userline;
private:

  HOBO_Server *m_parentserver;

  bool m_connectcompleted;

  JNL_IConnection *m_con;
  time_t m_lastmsgtime;
  bool m_haspinged;
};

#endif//_HOBO_HOBOCON_H_
