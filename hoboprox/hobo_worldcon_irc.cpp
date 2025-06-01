#include "hobo_worldcon_irc.h"

#include "../WDL/jnetlib/irc_util.h"
#include "../WDL/wdlcstring.h"

static const int g_global_limitconnect_amt = 30; // wait 6s before trying to connect to anything since the last outbound
static time_t g_global_limitconnect_lasttime;

HOBO_WorldCon_IRC::HOBO_WorldCon_IRC()
{
  m_reconnect_interval=15; // retry connect every 15s

  m_identname.Set("hobo");
  m_nickname.Set("hobo_user");
  m_con=0;
  m_state=NotConnected;
  m_lastconnect_time=0;
}

HOBO_WorldCon_IRC::~HOBO_WorldCon_IRC()
{
  delete m_con;
  m_con=0;
}

bool HOBO_WorldCon_IRC::IsConnected()
{
  if (!m_con||m_state!=Connected) return false;
  return true;
}

const char *HOBO_WorldCon_IRC::GetUniqueString()
{
  m_uniqstr.Set("IRC:");
  m_uniqstr.Append(m_servername.Get());

  return m_uniqstr.Get();
}


const char *HOBO_WorldCon_IRC::Connect(const char *dest)
{
  m_connect_str.Set(dest);
  char buf[1024];
  lstrcpyn_safe(buf,dest,sizeof(buf));

  char *tokens[16],*prefix=NULL;
  int tokens_valid=0;
  bool lastColon=false;
  ParseIRCMessage(buf,&prefix,tokens,&tokens_valid,&lastColon);
  if (prefix || tokens_valid < 1)
  {
    return "Usage: connect IRC servername[:port] [nickname] [identname|-] [password|-] [realname]";
  }

  m_servername.Set(tokens[0]);
  if (tokens_valid>1) m_nickname.Set(tokens[1]);
  if (tokens_valid>2 && strcmp(tokens[2],"-")) m_identname.Set(tokens[2]);
  if (tokens_valid>3 && strcmp(tokens[3],"-")) m_passstr.Set(tokens[3]);
  int x;
  m_realname.Set("");
  for(x=4;x<tokens_valid; x++)
  {
    if (x>4) m_realname.Append(" ");
    m_realname.Append(tokens[x]);
  }

  m_actual_nick.Set(m_nickname.Get());

  delete m_con;
  m_con=0;
  m_state=NotConnected;
  m_lastconnect_time = 0;

  return NULL;

}


void HOBO_WorldCon_IRC::Run(void (*MessageCallback)(const char *msg, void *userData), void *userData)
{
  time_t now = time(NULL);
  if (m_state == NotConnected && m_servername.Get()[0] &&
      now > m_lastconnect_time+m_reconnect_interval)
  {
    if (g_global_limitconnect_lasttime < now - g_global_limitconnect_amt)
    {
      g_global_limitconnect_lasttime = m_lastconnect_time = now;

      m_state=Connecting;
      m_con = new JNL_Connection;
      WDL_String tmp(m_servername.Get());
      int port=6667;
      char *p=strstr(tmp.Get(),":");
      if (p) { *p++=0; if (atoi(p)) port=atoi(p); }
      printf("connecting to %s %d\n",tmp.Get(),port);
      m_con->connect(tmp.Get(),port);
    }
  }

  if (m_con)
  {
    m_con->run();
    int st = m_con->get_state();
    if (st == JNL_Connection::STATE_ERROR || st == JNL_Connection::STATE_CLOSED)
    {
      delete m_con;
      m_con=0;
      m_state = NotConnected;
      printf(st == JNL_Connection::STATE_ERROR?"Error connecting\n":"Closed by foreign host\n");
      return;

    }
    if (m_state==Connecting)
    {
      if (st==JNL_Connection::STATE_CONNECTED && time(NULL)>m_lastconnect_time+3)
      {
        m_state=Connected;
        if (m_passstr.Get()[0])
          SendMsg("PASS %s",m_passstr.Get());
        SendMsg("NICK %s",m_actual_nick.Get()[0] ? m_actual_nick.Get() : m_nickname.Get());
        SendMsg("USER %s localhost irc.cockos.com :%s", m_identname.Get()[0]?m_identname.Get():"hoboprox",m_realname.Get()[0]?m_realname.Get():"hoboprox user");
        MessageCallback("!!!HOBOPROX_SERVER_RECONNECTED",userData); //notify parent to rejoin any channels
      }
    }

    if (m_state == Connected) while (m_con->recv_lines_available())
    {
      char buf[1024];
      if (!m_con->recv_line(buf,sizeof(buf)))
      {
        //printf("recv from server: %s\n",buf);
   //printf("IRC: got '%s' \n",buf);

        char *tokens[16];
        int tokens_valid=0;

        char *prefix=NULL;
        char buftmp[1024];
        strcpy(buftmp,buf);
        ParseIRCMessage(buftmp,&prefix,tokens,&tokens_valid,NULL);

        // if a numbered server message to us, treat this as our actual nick
        if (prefix && *prefix == ':')
        {
          if (tokens_valid > 1)
          {
            if (atoi(tokens[0]) > 0 && tokens[1][0]) 
              m_actual_nick.Set(tokens[1]);
            else if (!stricmp(tokens[0],"NICK") && tokens[1][0])
            {
              // check to see if prefix has no @ (meaning it is server-set)
              if (!strstr(prefix,"@")) m_actual_nick.Set(tokens[1]);

              // or prefix begins curnick!* or curnick@*
              else if (m_actual_nick.Get()[0] &&
                       !strnicmp(prefix,m_actual_nick.Get(),strlen(m_actual_nick.Get())) &&
                         (prefix[strlen(m_actual_nick.Get())] == '@' ||
                          prefix[strlen(m_actual_nick.Get())] == '!')
                      )
              {
                m_actual_nick.Set(tokens[1]);
              }
            }
          }
        }

        if (tokens_valid>0 && !stricmp(tokens[0],"PING"))
        {
          if (tokens_valid==1) SendMsg("PONG");
          else SendMsg("PONG :%s",tokens[1]);
        }
        else // convert messages
        {
          MessageCallback(buf,userData);

        }
      }
      else break;
    }
  }
}

bool HOBO_WorldCon_IRC::SendMsg(const char *fmt, ...)
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
//printf("DEBUG| SENDING: '%s'\n",buf);

    if ((int)strlen(buf) <= m_con->send_bytes_available())
    {
      strcpy(buf+written,"\r\n");
      m_con->send_string(buf);
      return true;
    }
  }
  return false;
}

