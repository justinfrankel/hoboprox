#include "hobo_server.h"
#include "../WDL/rng.h"

#include "../WDL/jnetlib/irc_util.h"
#include "../WDL/lineparse.h"
#include "../WDL/wdltypes.h"
#include "../WDL/wdlcstring.h"


static bool HasToken(const char **ptr, const char *tok)
{
  int l = strlen(tok);
  if (!strnicmp(*ptr,tok,l) && ((*ptr)[l]==0 || (*ptr)[l]==' '))
  {
    (*ptr) += l;
    while (**ptr == ' ') (*ptr)++;
    return true;
  }
  return false;
}
static bool GetTokenToBuf(const char **ptr, char *buf, int buflen)
{
  const char *p = *ptr;
  while (**ptr && **ptr != ' ') (*ptr)++;

  int l=wdl_min(buflen-1,*ptr - p);
  if (l<0)l=0;
  memcpy(buf,p,l);
  buf[l]=0;
  while (**ptr == ' ') (*ptr)++;
  return l>0;
}

HOBO_ChannelRec::HOBO_ChannelRec() 
{ 
  m_hist_startidx=0;
  active=true;
}
HOBO_ChannelRec::~HOBO_ChannelRec() 
{
  hist.Empty(true); 
  rdpos_recs.Empty(true);
}


HOBO_ServerWorldCon::HOBO_ServerWorldCon(HOBO_Server *server, HOBO_WorldCon *con)
{
  m_server = server;
  m_con=con;
}
HOBO_ServerWorldCon::~HOBO_ServerWorldCon()
{
  delete m_con;
  m_channels.Empty(true,free);
}

bool HOBO_ServerWorldCon::SendMsg(const char *fmt, ...)
{
  if (!m_con) return false;

  char buf[1024];
  va_list arglist;
	va_start(arglist, fmt);
  #ifdef _WIN32
	int written = _vsnprintf(buf, sizeof(buf)-16, fmt, arglist);
  #else
	int written = vsnprintf(buf, sizeof(buf)-16, fmt, arglist);
  #endif
  if (written < 0) written = 0;
  else if (written > 1023) written=1023;
  buf[written]=0;
	va_end(arglist);

  
  if (m_server && m_server->m_debugserverlogfp)
  {
    fprintf(m_server->m_debugserverlogfp,"SEND '%s'\n",buf);
    fflush(m_server->m_debugserverlogfp);
  }

  return m_con->SendMsg("%s",buf);

}

static void SimplifyNicknameString(char *out, const char *in, int outsize)
{
  lstrcpyn_safe(out,in,outsize);
  char *p=out;
  while (*p && *p != '!' && *p != '@') p++;
  *p=0;
}

void HOBO_ServerWorldCon::LogMessage(const char *type, const char *src, const char *dest, const char *msg, bool isMeSending)
{
  int x;
  const char *cname = dest;
  char tmpbuf[512];
  if (cname[0]!='#' && cname[0] != '&') 
  {
    if (!m_server->m_history_track_privmsg) return;

    if (!isMeSending)
      cname = src; // privmsg to me, list under the user's name

    SimplifyNicknameString(tmpbuf,cname,sizeof(tmpbuf));
    cname=tmpbuf;
  }

  HOBO_ChannelRec *rec=NULL;
  for(x=0;x<m_channels.GetSize();x++)
  {
    rec=m_channels.Get(x);
    if (!stricmp(rec->name.Get(),cname)) break;
    rec=NULL;
  }
  if (!rec)
  {
    if (m_channels.GetSize()>128) return; // bleh for now
    rec = new HOBO_ChannelRec;
    rec->name.Set(cname);
    rec->active=true;
    m_channels.Add(rec);

    if (cname[0] == '#' || cname[0] == '&')
      m_server->SaveState();
  }
  WDL_String *s=NULL;
  if (rec->hist.GetSize()>=m_server->m_history_lines) 
  {
    s=rec->hist.Get(0);
    rec->hist.Delete(0);
    rec->m_hist_startidx++;
  }
  if (!s) s=new WDL_String(256);
  if (s)
  {
    char datestr[256];
    strcpy(datestr,"?");
    time_t t= time(NULL);
    struct tm *now=localtime(&t);
    if (now) strftime(datestr,sizeof(datestr),"%Y/%m/%d@%H:%M",now);

    if (src&&*src) s->SetFormatted(512,"%s :%s",datestr,src);
    else s->Set(datestr);

    s->AppendFormatted(1024, " %s %s :%s",type, dest,msg);
    
    rec->hist.Add(s);

    int x,i;
    for (i=0;i<m_server->m_hobocons.GetSize();i++)
    {
      HOBO_HoboCon *c = m_server->m_hobocons.Get(i);
      for(x=0;x<rec->rdpos_recs.GetSize();x++)
      {
        HOBO_ChannelRec::IdentLastRdPosRec *r = rec->rdpos_recs.Get(x);
        if (!stricmp(r->name.Get(),c->m_realnameident.Get()))
        {
          r->m_lastrdidx = rec->m_hist_startidx + rec->hist.GetSize();
//          printf("updating realname ident for chan '%s' for '%s'\n",rec->name.Get(),
  //          c->m_realnameident.Get());
          break;
        }
      }
      if (x>=rec->rdpos_recs.GetSize() && x < 128/*sanity*/)
      {
        HOBO_ChannelRec::IdentLastRdPosRec *r  = new HOBO_ChannelRec::IdentLastRdPosRec;
        r->m_lastrdidx = rec->m_hist_startidx + rec->hist.GetSize();
        r->name.Set(c->m_realnameident.Get());
//        printf("adding realname ident for chan '%s' for '%s'\n",rec->name.Get(),
  //        c->m_realnameident.Get());
        rec->rdpos_recs.Add(r);
      }
      if (x>=128)
        printf("errrr overflow sanityf heck\n");
    }

    // remove any outdated recs
    for(x=0;x<rec->rdpos_recs.GetSize();x++)
    {
      if (rec->rdpos_recs.Get(x)->m_lastrdidx < rec->m_hist_startidx)
        rec->rdpos_recs.Delete(x--,true);
    }
  }
}

void HOBO_ServerWorldCon::WorldConCallback(const char *msg, void *userData)
{
  HOBO_ServerWorldCon *_this = (HOBO_ServerWorldCon*)userData;
  HOBO_Server *server = _this->m_server;

  if (server->m_debugserverlogfp)
  {
    fprintf(server->m_debugserverlogfp,"RECV '%s'\n",msg);
    fflush(server->m_debugserverlogfp);
  }

  if (msg && !strcmp(msg,"!!!HOBOPROX_SERVER_RECONNECTED"))
  {
    // re-join channels on server
    int x;
    for(x=0;x<_this->m_channels.GetSize();x++)
    {
      HOBO_ChannelRec *cr=_this->m_channels.Get(x);
      if (cr->active)
      {
        const char *ch = cr->name.Get();

        bool isChan = ch[0] == '&' || ch[0] == '#';
        if (isChan)
        {
          _this->SendMsg("JOIN :%s",ch);
        }
      }
    }
    return;
  }

  char *tokens[16];
  int tokens_valid=0;

  char *prefix=NULL;
  char buftmp[1024];
  lstrcpyn_safe(buftmp,msg,sizeof(buftmp));
  ParseIRCMessage(buftmp,&prefix,tokens,&tokens_valid,NULL);

  bool isPrefixMe = false;

  if (prefix && *prefix == ':')
  {
    if (tokens_valid > 1 && atoi(tokens[0]) > 0) {/*numbered server message */ }
    else 
    {
      const char *actnick = _this->m_con->GetNick();
      int nn=strlen(actnick);
      if (nn>0 && !strncmp(prefix+1,actnick,nn) &&
        (prefix[1+nn] == ' ' || prefix[1+nn] == 0 || prefix[1+nn] == '!' || prefix[1+nn] == '@'))
        isPrefixMe=true;
    }
  }

  if (isPrefixMe && tokens_valid>1)
  {
    if (!stricmp(tokens[0],"JOIN"))
    {
      int i;
      for(i=0;i<_this->m_channels.GetSize();i++)
        if (!stricmp(_this->m_channels.Get(i)->name.Get(),tokens[1])) 
        {
          if (!_this->m_channels.Get(i)->active)
          {
            _this->m_channels.Get(i)->active=true;
            server->SaveState();
          }
          break;
        }
      if (i==_this->m_channels.GetSize())
      {
        HOBO_ChannelRec *rec= new HOBO_ChannelRec;
        rec->name.Set(tokens[1]);
        rec->active=true;
        _this->m_channels.Add(rec);
        server->SaveState();
      }
    }
    else if (!stricmp(tokens[0],"PART") ||!stricmp(tokens[0],"KICK"))
    {
      int i;
      int cnt=0;
      for(i=0;i<_this->m_channels.GetSize();i++)
        if (!stricmp(_this->m_channels.Get(i)->name.Get(),tokens[1])) 
        {
          if (_this->m_channels.Get(i)->active)
          {
            _this->m_channels.Get(i)->active=false;
            cnt++;
          }
        }
      if (cnt) server->SaveState();

    }
  }
  // todo: manage list of server's channels, names, recent history

  if (tokens_valid>2 && !stricmp(tokens[0],"PRIVMSG") && prefix && *prefix)
  {
    if (tokens[2][0] != 1) // ignore CTCP
      _this->LogMessage(tokens[0],prefix+1,tokens[1],tokens[2],false);
    else
    {
      if (!strncmp(tokens[2]+1,"ACTION ",7))
        _this->LogMessage(tokens[0],prefix+1,tokens[1],tokens[2],false);
    }
  }

  int x;
  for (x=0;x < server->m_hobocons.GetSize(); x++)
  {
    HOBO_HoboCon *wc = server->m_hobocons.Get(x);
    wc->SendMsg("%s",msg);
  }
}

void HOBO_ServerWorldCon::UpdateStateToCurrent(HOBO_HoboCon *con)
{
  int x;
  for(x=0;x<m_channels.GetSize();x++)
  {
    HOBO_ChannelRec *cr=m_channels.Get(x);
    if (cr->active)
    {
      const char *ch = cr->name.Get();

      bool isChan = ch[0] == '&' || ch[0] == '#';
      
      if (isChan)
      {
        const char *nick = m_con->GetNick();
        con->SendMsg(":%s!%s@hobo JOIN :%s",nick,nick,ch);
        if (m_con) SendMsg("NAMES %s",ch); // server will notify clients of names
      }

      const char *msgsrc = "HOBOprox";
      const char *msgdst = ch;
      if (!isChan)
      {
        msgsrc = ch;
        msgdst = m_con->GetNick();
      }

      HOBO_Server::identLineConfig *lc = m_server->getLineConfig(con->m_realnameident.Get());

      // try to show max lines
      int spos = cr->hist.GetSize() - lc->m_history_maxlines;

      int a;
//      printf("searching for realname ident for chan '%s' for '%s'\n",cr->name.Get(),
  //      con->m_realnameident.Get());
      for(a=0;a<cr->rdpos_recs.GetSize();a++)
      {
        HOBO_ChannelRec::IdentLastRdPosRec *r = cr->rdpos_recs.Get(a);
        if (!stricmp(r->name.Get(),con->m_realnameident.Get()))
        {
          if (r->m_lastrdidx > cr->m_hist_startidx)
          {
//            printf("using realname ident for chan '%s' for '%s'\n",cr->name.Get(),
  //            con->m_realnameident.Get());
            spos = (int) (r->m_lastrdidx - cr->m_hist_startidx);
    //        printf("used offset %d, minlines=%d\n",spos,lc->m_history_minlines);

            if (spos < cr->hist.GetSize() - lc->m_history_maxlines) spos=cr->hist.GetSize() - lc->m_history_maxlines; // never show more than this many lines
            if (spos > cr->hist.GetSize() - lc->m_history_minlines) spos=cr->hist.GetSize() - lc->m_history_minlines; // always show at least this many lines, if avaliable
          }
          r->m_lastrdidx = cr->m_hist_startidx + cr->hist.GetSize(); // history is now up to date. might be better to send this later once we've confirmed everything got sent...
          break;
        }
      }
      if (a>=cr->rdpos_recs.GetSize())
      {
        // we need to add this channel
        HOBO_ChannelRec::IdentLastRdPosRec *r = new HOBO_ChannelRec::IdentLastRdPosRec;
        r->m_lastrdidx = cr->m_hist_startidx + cr->hist.GetSize();
        r->name.Set(con->m_realnameident.Get());
        cr->rdpos_recs.Add(r);
      }

      int i;

      if (spos<0)spos=0;
      char lasttime[256];
      lasttime[0]=0;
      int replaycnt=0;
      for(i=0;;i++)
      {
        WDL_String *s=cr->hist.Get(spos++);
        if (!s) break;

        const char *p=s->Get();
        char thistime[256];
        thistime[0]=0;
        if (GetTokenToBuf(&p,thistime,sizeof(thistime)))
        {
          char *tokens[16];
          int tokens_valid=0;
          char *prefix=NULL;
          char buftmp[1024];
          lstrcpyn_safe(buftmp,p,sizeof(buftmp));
          ParseIRCMessage(buftmp,&prefix,tokens,&tokens_valid,NULL);

          // parse out 
          if (tokens_valid>2)
          {
            int tl = strlen(thistime);
            if (tl > strlen(lasttime)) tl=strlen(lasttime);
            if (tl<1||memcmp(thistime,lasttime,tl-1))
            {
              strcpy(lasttime,thistime);
              con->SendMsg(":%s PRIVMSG %s :------------ %s %s ------------",msgsrc,msgdst,replaycnt++ == 0 ? "Replay started @": "Replay @", thistime);
            }
            con->SendMsg(":%s %s %s :%s",isChan && prefix ? prefix+1 : msgsrc,tokens[0],msgdst,tokens[2]);
          }
        }          
      }
      if (replaycnt) 
        con->SendMsg(":%s PRIVMSG %s :------------ Replay finished! ------------",msgsrc,msgdst);

    }
  }
  // stream current state to client (this would be join, names, and recent lines of log)
}

void HOBO_ServerWorldCon::OnOutMessage(const char *msg)
{
  char *tokens[16];
  int tokens_valid=0;

  char *prefix=NULL;
  char buftmp[1024];
  lstrcpyn_safe(buftmp,msg,sizeof(buftmp));
  ParseIRCMessage(buftmp,&prefix,tokens,&tokens_valid,NULL);

  // log our own stuff here
  if (tokens_valid>2 && !stricmp(tokens[0],"PRIVMSG") && m_con)
    LogMessage(tokens[0],m_con->GetNick(),tokens[1],tokens[2],true);
}


HOBO_Server::HOBO_Server()
{
  m_debuglogfp = NULL;
  m_debugserverlogfp=NULL;
  m_history_track_privmsg=true;
  m_history_lines=3000;
  m_linecfgs.Add(new identLineConfig);
}

HOBO_Server::identLineConfig *HOBO_Server::getLineConfig(const char *identstr)
{
  int x;
  if (identstr&&*identstr) for(x=1;x<m_linecfgs.GetSize();x++)
  {
    if (!stricmp(m_linecfgs.Get(x)->str.Get(),identstr))
    {
      return m_linecfgs.Get(x);
    }
  }

  return m_linecfgs.Get(0);
}

HOBO_Server::~HOBO_Server()
{
  m_worldcons.Empty(true);
  m_hobocons.Empty(true);
  m_linecfgs.Empty(true);
  if(m_debuglogfp) fclose(m_debuglogfp); 
  if (m_debugserverlogfp) fclose(m_debugserverlogfp);
  m_debuglogfp=0; 
  m_debugserverlogfp=0;
}

void HOBO_Server::Run()
{
  int i;
  for (i = 0; i < m_worldcons.GetSize(); i ++) 
    m_worldcons.Get(i)->m_con->Run(HOBO_ServerWorldCon::WorldConCallback,m_worldcons.Get(i));

  for (i = 0; i < m_hobocons.GetSize(); i ++)
  {
    HOBO_HoboCon *hc = m_hobocons.Get(i);

    if (hc->Run(HoboConCallback,this)) m_hobocons.Delete(i--,true);

  }
}

void HOBO_Server::AddClient(JNL_IConnection *con, const char *nickline, const char *userline)
{
  if (con) 
  {
    HOBO_HoboCon *hc=new HOBO_HoboCon(con,nickline,userline,this);
    m_hobocons.Add(hc);
  }
}


void HOBO_Server::HoboConCallback(const char *msg, void *userData, HOBO_HoboCon *con)
{
  HOBO_Server *_this = (HOBO_Server*)userData;

  if (msg && !strcmp(msg,"!!!HOBOPROX_CLIENT_CONNECT_COMPLETE"))
  {  
    int i;
    for (i = 0; i < _this->m_worldcons.GetSize(); i ++) 
    {
      if (!i)
          con->SendMsg(":hobo 001 %s :Welcome to hoboprox (connections are up)",_this->m_worldcons.Get(i)->m_con->GetNick());
      _this->m_worldcons.Get(i)->UpdateStateToCurrent(con);

    }


    if (!i) // fallback if no connections up
    {
      if (con->m_nickline.Get()[0])
      {
        char buf[1024];
        char *tokens[16];
        int tokens_valid=0;
        char *prefix=NULL;
        lstrcpyn_safe(buf,con->m_nickline.Get(),sizeof(buf));
        ParseIRCMessage(buf,&prefix,tokens,&tokens_valid,NULL);

        con->SendMsg(":hobo 001 %s :Welcome to hoboprox (no connections are up)",tokens_valid>1 ? tokens[1] : "user");
      }
      // only send this if no connectoins are up
      con->TalkToUser("Welcome to HOBOprox. type HELP for help.");
    }
    return;
  }

  // message from client!
  int x;
  for (x=0;x < _this->m_worldcons.GetSize(); x++)
  {
    HOBO_ServerWorldCon *wc = _this->m_worldcons.Get(x);
    if (wc && wc->m_con && wc->m_con->IsConnected())
    {
      wc->SendMsg("%s",msg);
      wc->OnOutMessage(msg);
    }
  }

}

void HOBO_Server::AddWorldCon(HOBO_WorldCon *con)
{
  m_worldcons.Add(new HOBO_ServerWorldCon(this,con));
}

void HOBO_Server::HandleHOBOproxPrivMsg(HOBO_HoboCon *con, const char *fulbuf, const char *command)
{
  if (HasToken(&command,"STAT"))
  {
    con->TalkToUser("Status: %d clients, %d servers",m_hobocons.GetSize(),m_worldcons.GetSize());
    int x;
    for (x = 0; x < m_worldcons.GetSize(); x++)
    {
      HOBO_ServerWorldCon *wc = m_worldcons.Get(x);
      if (wc->m_con)
      {
        con->TalkToUser("Server %d '%s': nick=%s",x+1,wc->m_con->GetUniqueString(),wc->m_con->GetNick());
      }
    }
    // todo: detailed stats
  }
  else if (HasToken(&command,"REHASH"))
  {
    extern int g_need_config_reload;
    g_need_config_reload=1;
  }
  else if (HasToken(&command,"DISCONNECT"))
  {
    int x=atoi(command);
    if (m_worldcons.Get(x-1))
    {
      m_worldcons.Delete(x-1,true);
      con->TalkToUser("DISCONNECT: deleted server %d",x);
      SaveState();
    }
    else
      con->TalkToUser("DISCONNECT: need valid server index");
  }
  else if (HasToken(&command,"CONNECT"))
  {
    char type[128];
    
    if (GetTokenToBuf(&command,type,sizeof(type)))
    {
      if (m_worldcons.GetSize())
      {
        con->TalkToUser("CONNECT: only one connection is currently supported. try DISCONNECT first");
      }
      else
      {
        HOBO_WorldCon *newc = HOBO_WorldCon_CreateByType(type);
        if (!newc) con->TalkToUser("CONNECT: Unknown type '%s'",type);
        else
        {
          const char *err = newc->Connect(command);
          if (err)
          {
            con->TalkToUser("CONNECT: %s",err);
            delete newc;
          }
          else
          {
            AddWorldCon(newc);
            SaveState();
          }
        }
      }
    }
    else
      con->TalkToUser("Usage: CONNECT [IRC|???] [...]");
  }
  else if (HasToken(&command,"FORGET"))
  {
    char type[128];
    
    if (GetTokenToBuf(&command,type,sizeof(type)))
    {
      int x,cnt=0;
      for (x=0;x<m_worldcons.GetSize();x++)
      {
        HOBO_ServerWorldCon *c = m_worldcons.Get(x);
        int i;
        if (c) for (i=0;i<c->m_channels.GetSize();i++)
        {
          HOBO_ChannelRec *r = c->m_channels.Get(i);
          if (r && !stricmp(r->name.Get(),type))
          {
            c->m_channels.Delete(i--,true);
            cnt++;
          }
        }
      }
      con->TalkToUser("FORGOT %d channels\n",cnt);
    }
    else con->TalkToUser("Usage: FORGET username|channelname");
  }
  else if (HasToken(&command,"HELP"))
  {
    con->TalkToUser("Commands:");
    con->TalkToUser("CONNECT DISCONNECT STAT REHASH");
  }
  else
  {
    con->TalkToUser("Unknown command, try HELP");
  }
  
}

void HOBO_Server::SaveState()
{
  WDL_String s(m_name.Get());
  s.Append("--userconfig.cfg");
  FILE *fp=fopen(s.Get(),"w");
  if (!fp) return;
  int i;
  for(i=0;i<m_worldcons.GetSize(); i++)
  {
    HOBO_ServerWorldCon *wc = m_worldcons.Get(i);
    if (wc&&wc->m_con)
    {
      fprintf(fp,"CONNECT `%s` `%s`\n",wc->m_con->GetProtocol(),wc->m_con->GetConnectString());
      int x;
      for(x=0;x<wc->m_channels.GetSize();x++)
      {
        HOBO_ChannelRec *cr = wc->m_channels.Get(x);
        if (cr->active && (cr->name.Get()[0] == '&'||cr->name.Get()[0] == '#'))
        {
          fprintf(fp,"CHAN \"%s\"\n",cr->name.Get());
        }
      }
    }
  }
  fclose(fp);
}

void HOBO_Server::LoadState() // this will only ever be called on a fresh instance
{
  WDL_String s(m_name.Get());
  s.Append("--userconfig.cfg");
  FILE *fp = fopen(s.Get(),"r");
  if (!fp) return;

  HOBO_ServerWorldCon *last_success=NULL;
  bool comment_state=false;
  for (;;)
  {
    char buf[4096];
    buf[0]=0;
    fgets(buf,sizeof(buf),fp);
    if (!buf[0]) break;
    while (buf[0] && (buf[strlen(buf)-1] == '\n' || buf[strlen(buf)-1] == '\r'))  buf[strlen(buf)-1]=0;
    LineParser lp(comment_state);
    if (lp.parse(buf)||lp.getnumtokens()<=0) continue;

    if (!stricmp(lp.gettoken_str(0),"CONNECT"))
    {
      last_success=0;
      if (lp.getnumtokens()>=3)
      {
        const char *proto = lp.gettoken_str(1);
        const char *cstr = lp.gettoken_str(2);
        HOBO_WorldCon *wc = HOBO_WorldCon_CreateByType(proto);
        if (wc)
        {
          if (!wc->Connect(cstr))
          {
            m_worldcons.Add(last_success=new HOBO_ServerWorldCon(this,wc));          
          }
          else delete wc;
        }
      }
    }
    else if (!stricmp(lp.gettoken_str(0),"CHAN") && last_success)
    {
      if (lp.getnumtokens()>1)
      {
        HOBO_ChannelRec *cr= new HOBO_ChannelRec;
        cr->name.Set(lp.gettoken_str(1));
        cr->active=true;
        last_success->m_channels.Add(cr); // rejoin will happen automatically in theory
      }
    }
  }

  fclose(fp);
}
