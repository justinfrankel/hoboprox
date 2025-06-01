#ifndef _HOBO_SERVER_H_
#define _HOBO_SERVER_H_


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../WDL/ptrlist.h"
#include "../WDL/wdltypes.h"
#include "hobo_worldcon.h"
#include "hobo_hobocon.h"

#define HOBO_IRCNAME "HOBOprox!hobo@hobo"

#define HOBO_CONNECT_BUFFERSIZE (16*65536) // 1mb



class HOBO_ChannelRec
{
public:
  HOBO_ChannelRec();
  ~HOBO_ChannelRec(); 

  WDL_String name;
  bool active;

  WDL_UINT64 m_hist_startidx;
  WDL_PtrList<WDL_String> hist;

  class IdentLastRdPosRec
  {
    public:
      IdentLastRdPosRec() { }
      ~IdentLastRdPosRec() { }

      WDL_String name;
      WDL_UINT64 m_lastrdidx;
  };
  WDL_PtrList<IdentLastRdPosRec> rdpos_recs;
};

class HOBO_Server;

class HOBO_ServerWorldCon
{
public:
  HOBO_ServerWorldCon(HOBO_Server *server, HOBO_WorldCon *con);
  ~HOBO_ServerWorldCon();

  void LogMessage(const char *type, const char *src, const char *dest, const char *msg, bool isMeSending);

  void OnOutMessage(const char *msg);
  void UpdateStateToCurrent(HOBO_HoboCon *con);
  static void WorldConCallback(const char *msg, void *userData);

  bool SendMsg(const char *fmt, ...);

  WDL_PtrList<HOBO_ChannelRec> m_channels;

  HOBO_Server *m_server;
  HOBO_WorldCon *m_con;
};

class HOBO_Server // manages a list of HOBO_WorldCon, and communicates with a hobo client
{
  public:
    HOBO_Server();
    ~HOBO_Server();

    WDL_String m_name; // used by parent
    WDL_String m_password; // used to verify identity

    bool m_history_track_privmsg;
    int m_history_lines;
    FILE *m_debuglogfp,*m_debugserverlogfp;

    class identLineConfig
    {
    public:
      identLineConfig() 
      { 
        m_history_minlines=0;
        m_history_maxlines=100;
      }
      ~identLineConfig() { } 
      WDL_String str;
      int m_history_minlines; // min lines to show on reconnect (0 for auto)
      int m_history_maxlines; // max lines to show on reconnect
    };
    WDL_PtrList<identLineConfig> m_linecfgs;

    identLineConfig *getLineConfig(const char *identstr);

    // these save state for each worldcon + channels
    void SaveState(); 
    void LoadState();

    void AddClient(JNL_IConnection *con, const char *nickline, const char *userline); // creates a HOBO_HoboCon

    void AddWorldCon(HOBO_WorldCon *con);

    void Run();

    WDL_PtrList<HOBO_ServerWorldCon> m_worldcons;
    WDL_PtrList<HOBO_HoboCon> m_hobocons;

    static void HoboConCallback(const char *msg, void *userData, HOBO_HoboCon *con);


    void HandleHOBOproxPrivMsg(HOBO_HoboCon *con, const char *fulbuf, const char *command);
};



#endif //_HOBO_SERVER_H_
