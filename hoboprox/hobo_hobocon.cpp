#include "hobo_hobocon.h"
#include "hobo_server.h"

#include "../WDL/jnetlib/irc_util.h"

#define CON_TIMEOUT 60


HOBO_HoboCon::HOBO_HoboCon(JNL_IConnection *con, const char *nickline, const char *userline,
                           HOBO_Server *parentserver)
{
  m_parentserver=parentserver;
  time(&m_lastmsgtime);
  m_haspinged=false;
  m_con=con;
  m_connectcompleted=0;
  if (nickline && *nickline) m_nickline.Set(nickline);
  if (userline && *userline) m_userline.Set(userline);

  if (m_parentserver->m_debuglogfp)
  {
    char ipbuf[512];
    if (m_con) JNL::addr_to_ipstr(m_con->get_remote(),ipbuf,sizeof(ipbuf));
    else strcpy(ipbuf,"no-ip");
    fprintf(m_parentserver->m_debuglogfp,"%s CONNECT '%s' '%s'\n",ipbuf,m_nickline.Get(),m_userline.Get());
    fflush(m_parentserver->m_debuglogfp);
  }

}

HOBO_HoboCon::~HOBO_HoboCon()
{
  if (m_parentserver->m_debuglogfp)
  {
    char ipbuf[512];
    if (m_con) JNL::addr_to_ipstr(m_con->get_remote(),ipbuf,sizeof(ipbuf));
    else strcpy(ipbuf,"no-ip");
    fprintf(m_parentserver->m_debuglogfp,"%s DISCONNECT\n",ipbuf);
    fflush(m_parentserver->m_debuglogfp);
  }
  delete m_con;
  m_con=0;
}

bool HOBO_HoboCon::Run(void (*MessageCallBack)(const char *msg, void *userData, HOBO_HoboCon *con), void *userData) // return TRUE if failed
{
  if (!m_con) return true;

  if (!m_connectcompleted && m_nickline.Get()[0] && m_userline.Get()[0])
  {
    //m_identname
    m_connectcompleted=true;
    char tmp[1024];
    strcpy(tmp,m_userline.Get());
    char *prefix=NULL,*tokens[16];
    int numtokens=0;
    bool colon=false;
    ParseIRCMessage(tmp,&prefix,tokens,&numtokens,&colon);
    if (numtokens>4)  m_realnameident.Set(tokens[4]);
    else if (numtokens>1) m_realnameident.Set(tokens[1]);

    MessageCallBack("!!!HOBOPROX_CLIENT_CONNECT_COMPLETE",userData,this);
  }

  int max_loops=4;

  if (0){
    FILE *fp=fopen("C:/hobo_send.txt","r");
    if (fp)
    {
      char buf[1024]={0,};
      fgets(buf,sizeof(buf),fp);
      fclose(fp);
      unlink("C:/hobo_send.txt");
      char *p=buf;
      while (*p) p++;
      p--;
      while (p>=buf && (*p == '\r' || *p == '\n')) p--;
      p[1]=0;
      if (buf[0]) SendMsg("%s",buf);
    }
  }
  while (max_loops-->0)
  {
    m_con->run();
    int st = m_con->get_state();
    if (st == JNL_Connection::STATE_CLOSING || st == JNL_Connection::STATE_CLOSED) return true;

    int did_stuff=0;

    while (m_con->recv_lines_available()>0)
    {
      char buf[1024];
      buf[0]=0;
      m_con->recv_line(buf,sizeof(buf));
      did_stuff|=1;
      buf[1023]=0;
      if (buf[0])
      {
        char tmp[1024];
        strcpy(tmp,buf);
        char *prefix=NULL,*tokens[16];
        int numtokens=0;
        bool colon=false;
        ParseIRCMessage(tmp,&prefix,tokens,&numtokens,&colon);

        if (numtokens>0)
        {
          if (m_parentserver->m_debuglogfp)
          {
            char ipbuf[512];
            JNL::addr_to_ipstr(m_con->get_remote(),ipbuf,sizeof(ipbuf));
            fprintf(m_parentserver->m_debuglogfp,"%s RECV '%s'\n",ipbuf,buf);
            fflush(m_parentserver->m_debuglogfp);
          }
          if (!stricmp(tokens[0],"QUIT"))  
          {
            m_con->close(1);
            break;
          }

          if (!m_connectcompleted && !stricmp(tokens[0],"NICK")) 
          {
            m_nickline.Set(buf);
          }

          if (!stricmp(tokens[0],"USER"))  // dont allow passthrough of USER line
          {
            if (!m_connectcompleted) m_userline.Set(buf);
          }
          else if (
                stricmp(tokens[0],"PONG")&&
                stricmp(tokens[0],"QUIT") // ignore quit
                )
          {
            if (numtokens>2 && !stricmp(tokens[0],"PRIVMSG") && !stricmp(tokens[1],"HOBOprox"))
              m_parentserver->HandleHOBOproxPrivMsg(this,buf,tokens[numtokens-1]);
            else
            {
              // todo: all kinds of translation
              MessageCallBack(buf,userData,this);
            }
          }
        }
      }
    }

    time_t now;
    time(&now);
    if (did_stuff&1) { m_lastmsgtime=now; m_haspinged=false; }
    else
    {
      if (now - m_lastmsgtime > CON_TIMEOUT/2 && !m_haspinged)
      {
        m_haspinged=true;
        SendMsg("PING :HOBOprox");
      }
      if (now - m_lastmsgtime > CON_TIMEOUT)
      {
        MessageCallBack(":HOBOprox NOTICE user :a remote connection timed out",userData,this);
        delete m_con;
        m_con=0;
        return true;
      }
    }
    if (!did_stuff) break;
  }

  return false;
}

bool HOBO_HoboCon::SendMsg(const char *fmt, ...) // true on success
{
  if (m_con)
  {
    char buf[1024];
  	va_list arglist;
		va_start(arglist, fmt);
    #ifdef _WIN32
		int written = _vsnprintf(buf, sizeof(buf)-16, fmt, arglist);
    #else
		int written = vsnprintf(buf, sizeof(buf)-16, fmt, arglist);
    #endif
    if (written < 0) written = 0;
    else if (written > 510) written=510;
    buf[written]=0;
		va_end(arglist);

    if ((int)strlen(buf) <= m_con->send_bytes_available())
    {
      if (m_parentserver->m_debuglogfp)
      {
        char ipbuf[512];
        JNL::addr_to_ipstr(m_con->get_remote(),ipbuf,sizeof(ipbuf));
        fprintf(m_parentserver->m_debuglogfp,"%s SEND '%s'\n",ipbuf,buf);
        fflush(m_parentserver->m_debuglogfp);
      }
      strcpy(buf+written,"\r\n");
      m_con->send_string(buf);
    }
    else
    {
      if (m_parentserver->m_debuglogfp)
      {
        char ipbuf[512];
        JNL::addr_to_ipstr(m_con->get_remote(),ipbuf,sizeof(ipbuf));
        fprintf(m_parentserver->m_debuglogfp,"%s FAILEDSENDBUFFERFULL '%s'\n",ipbuf,buf);
        fflush(m_parentserver->m_debuglogfp);
      }
    }
  }
  else
  {
    if (m_parentserver->m_debuglogfp) 
    {
      fprintf(m_parentserver->m_debuglogfp,"? NOCONSEND fmt='%s'",fmt);
      fflush(m_parentserver->m_debuglogfp);
    }
  }


  return true;
}

bool HOBO_HoboCon::TalkToUser(const char *fmt, ...) // true on success
{
  if (m_con)
  {
    char buf[1024];
  	va_list arglist;
		va_start(arglist, fmt);
    #ifdef _WIN32
		int written = _vsnprintf(buf, sizeof(buf)-16, fmt, arglist);
    #else
		int written = vsnprintf(buf, sizeof(buf)-16, fmt, arglist);
    #endif
    if (written < 0) written = 0;
    else if (written > 510) written=510;
    buf[written]=0;
		va_end(arglist);

    WDL_String p;
    const char *nick = m_nickline.Get();
    if(!nick || !nick[0]) nick = "user";
    else if(strstr(nick, "NICK ") == nick) nick+=5;
    p.SetFormatted(510, ":" HOBO_IRCNAME " PRIVMSG %s :%s", nick, buf);

    SendMsg("%s", p.Get());
  }
  return true;
}
