#include <stdio.h>


#include "hobo_server.h"
#include "hobo_worldcon.h"


#include "../WDL/jnetlib/irc_util.h"
#include "../WDL/lineparse.h"
#include "../WDL/wdlcstring.h"

#ifndef _WIN32
#define Sleep(x) usleep((x)*1000)
#endif

static bool g_done;

void sighandler_ignore(int sig) 
{ 
}

void sighandler(int sig)
{
	g_done=true;
}


struct incoming_rec
{
  incoming_rec(JNL_IConnection *c) 
  { 
    con=c; 
    time(&contime); 
    linerecvcnt=0; 
  }
  ~incoming_rec() { delete con; }

  int linerecvcnt;

  WDL_String nickline,userline;

  JNL_IConnection *con;
  time_t contime;
};


bool HOBO_wwwRun(JNL_IConnection *con, int port);


int g_port=6969;
int g_connect_timeout = 30;
JNL_Listen *g_listen;

int LoadConfig(WDL_PtrList<HOBO_Server> *servers, FILE *fp)
{
  int errcnt=0;
  WDL_PtrList<HOBO_Server> seen_servers;
  bool comment_state=false;
  HOBO_Server *last_server=NULL;
  for (;;)
  {
    char buf[4096];
    buf[0]=0;
    fgets(buf,sizeof(buf),fp);
    if (!buf[0]) break;
    while (buf[0] && (buf[strlen(buf)-1] == '\n' || buf[strlen(buf)-1] == '\r'))  buf[strlen(buf)-1]=0;
    LineParser lp(comment_state);
    if (lp.parse(buf)||lp.getnumtokens()<=0) continue;
    if (!stricmp(lp.gettoken_str(0),"server"))
    {
      if (lp.getnumtokens()==3)
      {
        const char *name = lp.gettoken_str(1);
        const char *pass = lp.gettoken_str(2);
        int x;
        for(x=0;x<servers->GetSize();x++)
        {
          if (!stricmp(servers->Get(x)->m_name.Get(),name))
          {
            printf("Updating server '%s'\n",servers->Get(x)->m_name.Get());
            servers->Get(x)->m_password.Set(pass);
            seen_servers.Add(last_server=servers->Get(x));
            last_server->m_linecfgs.Empty(true);
            last_server->m_linecfgs.Add(new HOBO_Server::identLineConfig);
            break;
          }
        }
        if (x >= servers->GetSize())
        {
          HOBO_Server *serv=new HOBO_Server;
          serv->m_name.Set(name);
          serv->m_password.Set(pass);
          seen_servers.Add(serv);
          printf("Adding server '%s'\n",name);
          serv->LoadState();
          servers->Add(last_server=serv);
        }

      }
      else
      {
        printf("config file: Usage: server <desc> <uniquepass>\n");
        errcnt++;
      }
    }
    else if (!stricmp(lp.gettoken_str(0),"www_root"))
    {
      extern char g_wwwroot[2048];
      if (lp.getnumtokens()==2)
        lstrcpyn_safe(g_wwwroot,lp.gettoken_str(1),sizeof(g_wwwroot));
    }
    else if (!stricmp(lp.gettoken_str(0),"port"))
    {
      if (lp.getnumtokens()==2 && lp.gettoken_int(1))
        g_port = lp.gettoken_int(1);
      else
      {
        printf("config file: Usage: port <portnum>\n");
        errcnt++;
      }
    }
    else if (!stricmp(lp.gettoken_str(0),"connect_timeout"))
    {
      if (lp.getnumtokens()==2 && lp.gettoken_int(1))
        g_connect_timeout = lp.gettoken_int(1);
      else
      {
        printf("config file: Usage: connect_timeout <seconds>\n");
        errcnt++;
      }
    }
    else if (!stricmp(lp.gettoken_str(0),"track_lines"))
    {
      if (lp.getnumtokens()==3)
      {
        if (!last_server) 
        {
          printf("config file: track_lines specified but no 'server' line\n");
          errcnt++;
        }
        else
        {
          last_server->m_history_lines = lp.gettoken_int(1);
          last_server->m_history_track_privmsg = !!lp.gettoken_int(1);
        }
      }
      else
      {
        errcnt++;
        printf("config file: Usage: track_lines <max_per_channel> <track_privmsg ? 1 : 0>\n");
      }
    }
    else if (!stricmp(lp.gettoken_str(0),"debug_log"))
    {
      if (lp.getnumtokens()==2||lp.getnumtokens()==3)
      {
        if (!last_server) 
        {
          printf("config file: debug_log specified but no 'server' line\n");
          errcnt++;
        }
        else
        {
          if (last_server->m_debuglogfp) fclose(last_server->m_debuglogfp);
          last_server->m_debuglogfp=0;
          if (last_server->m_debugserverlogfp) fclose(last_server->m_debugserverlogfp);
          last_server->m_debugserverlogfp=0;

          if (lp.gettoken_str(1)[0])
          {
            last_server->m_debuglogfp = fopen(lp.gettoken_str(1),"a+");
            if (!last_server->m_debuglogfp)
            {
              printf("config file: debug_log: error opening '%s'\n",lp.gettoken_str(1));
            }
          }
          if (lp.gettoken_str(2)[0])
          {
            last_server->m_debugserverlogfp = fopen(lp.gettoken_str(2),"a+");
            if (!last_server->m_debugserverlogfp)
            {
              printf("config file: debug_log: error opening '%s'\n",lp.gettoken_str(1));
            }
          }
        }
      }
      else
      {
        errcnt++;
        printf("config file: Usage: debug_log <filename>\n");
      }
    }
    else if (!stricmp(lp.gettoken_str(0),"reconnect_show"))
    {
      if (lp.getnumtokens()==4)
      {
        if (!last_server) 
        {
          printf("config file: reconnect_show specified but no 'server' line\n");
          errcnt++;
        }
        else
        {
          HOBO_Server::identLineConfig *p = last_server->getLineConfig(lp.gettoken_str(1));
          if (!p->str.Get()[0] && lp.gettoken_str(1)[0] && lp.gettoken_str(1)[0] != '*')
          {
            p=new HOBO_Server::identLineConfig;
            p->str.Set(lp.gettoken_str(1));
            last_server->m_linecfgs.Add(p);
          }
          p->m_history_minlines = lp.gettoken_int(2);
          p->m_history_maxlines = lp.gettoken_int(3);
        }
      }
      else
      {
        printf("config file: Usage: reconnect_show ['real name'|*] <min_lines|0> <max_lines>\n");
        errcnt++;
      }
    } 
    else
    {
      printf("unknown token '%s'\n",lp.gettoken_str(0));
      errcnt++;
    }
    

  }
  int x;
  for (x=0;x<servers->GetSize();x++)
  {
    if (seen_servers.Find(servers->Get(x))<0)
    {
      printf("Removing server '%s'\n",servers->Get(x)->m_name.Get());
      servers->Delete(x--,true);
    }
  }

  if (!g_listen || g_listen->port() != g_port)
  {
    delete g_listen;
    g_listen = new JNL_Listen(g_port);
    printf("Listening on %d\n",g_port);
  }
  return errcnt;
}

int base64decode(const char *src, unsigned char *dest, int destsize)
{
  unsigned char *olddest=dest;

  int accum=0;
  int nbits=0;
  while (*src)
  {
    int x=0;
    char c=*src++;
    if (c >= 'A' && c <= 'Z') x=c-'A';
    else if (c >= 'a' && c <= 'z') x=c-'a' + 26;
    else if (c >= '0' && c <= '9') x=c-'0' + 52;
    else if (c == '+') x=62;
    else if (c == '/') x=63;
    else break;

    accum <<= 6;
    accum |= x;
    nbits += 6;   

    while (nbits >= 8)
    {
      if (--destsize<0) break;
      nbits-=8;
      *dest++ = (char)((accum>>nbits)&0xff);
    }

  }
  return dest-olddest;
}

int g_need_config_reload;

int main(int argc, char **argv)
{
  const char *config_file = "hoboprox.cfg";
  if (argc>1) config_file=argv[1];
  WDL_PtrList<HOBO_Server> servers;
  WDL_PtrList<incoming_rec> incoming;

  JNL::open_socketlib();

  FILE *fp=fopen(config_file,"r");
  if (!fp)
  {
    printf("Usage: hoboprox [hoboprox.cfg]\n");
    return 0;
  }
  printf("Reading configuration from '%s'\n", config_file);
  if (LoadConfig(&servers,fp))
  {
    fclose(fp);
    goto bailOut;
  }
  fclose(fp);

  if (!servers.GetSize())
  {
    printf("Error: '%s' needs to have one or more 'server <name> <password>' strings\n", config_file);

    goto bailOut;
  }



#ifndef _WIN32
	signal(SIGINT,sighandler);
	signal(SIGQUIT,sighandler);
	signal(SIGPIPE,sighandler_ignore);
#endif


  while (!g_done)
  {
    if (g_need_config_reload)
    {
      g_need_config_reload=0;
      FILE *fp=fopen(config_file,"r");
      if (!fp) printf("Error reloading config file (couldn't open)\n");
      else
      {
        printf("Reloading configuration file from '%s':\n",config_file);
        LoadConfig(&servers,fp);
        printf("Finished config reload!\n");
        fclose(fp);
      }
    }

    Sleep(10);
    int i;
    for (i=0;i<servers.GetSize();i++) servers.Get(i)->Run();

    JNL_IConnection *con = g_listen->get_connect(HOBO_CONNECT_BUFFERSIZE,HOBO_CONNECT_BUFFERSIZE);
    if (con) incoming.Add(new incoming_rec(con));
    bool HOBO_wwwRun(WDL_PtrList<HOBO_Server> *servers, JNL_IConnection *con, int port);
    HOBO_wwwRun(&servers,NULL,0);

    for(i=0;i<incoming.GetSize();i++)
    {
      incoming_rec *rec = incoming.Get(i);
      if (time(NULL) > rec->contime+g_connect_timeout) 
      {
        incoming.Delete(i--,true);
        continue;
      }

      if (rec->con)
      {
        rec->con->run();
        int max_l = 16;
        while (rec->con->recv_lines_available()>0 && max_l-- > 0)
        {
          char buf[1024];
          buf[0]=0;

          if (!rec->linerecvcnt++)
          {
            int n = rec->con->recv_bytes_available();
            if (n > sizeof(buf)) n=sizeof(buf);
            if (n>4 && rec->con->peek_bytes(buf,n) == n && !memcmp(buf,"GET ",4))
            {
              char *p=buf;
              while (p < buf+n-5 && *p != '\r' && *p != '\n' && memcmp(p,"HTTP/",5)) p++;
              if (p < buf+n-5 && !memcmp(p,"HTTP/",5))
              {
                if (HOBO_wwwRun(&servers,rec->con,g_port))
                {
                  rec->con=0;
                }
                break; // 
              }
            }
          }

          if (!rec->con)
          {
            incoming.Delete(i--,true);
            break;
          }

          buf[0]=0;
          rec->con->recv_line(buf,1024);
          buf[1023]=0; // JNL's recv_line might be crappy
          if (!buf[0]) continue;

          char tmp[1024];
          strcpy(tmp,buf);
          char *tokens[16],*prefix=NULL;
          int tokens_valid=0;
          bool hadCol=false;
          ParseIRCMessage(tmp,&prefix,tokens,&tokens_valid,&hadCol);
          if (tokens_valid>0)
          {
            if (!stricmp(tokens[0],"NICK")) rec->nickline.Set(buf);
            else if (!stricmp(tokens[0],"USER")) rec->userline.Set(buf);
            else if (!stricmp(tokens[0],"PASS")) 
            {
              int x;
              for (x=0;x<servers.GetSize();x++)
              {
                HOBO_Server *server = servers.Get(x);

                if (!strcmp(buf+5,server->m_password.Get()))
                {
                  server->AddClient(rec->con,rec->nickline.Get(),rec->userline.Get());
                  rec->con=0; // keep the connection for us, woot
                  break;
                }
              }
              if (x == servers.GetSize())
              {
                printf("Invalid password\n");
              }
              incoming.Delete(i--,true);
              break;
            }
          } // valid tokens
        } // process input lines
      } // if raw connection present
    } // incoming connections
  } // main loop

bailOut:
  servers.Empty(true);
  incoming.Empty(true);
  delete g_listen;

  JNL::close_socketlib();

  return 0;
}

