#include <stdio.h>


#include "hobo_server.h"


#define JNETLIB_WEBSERVER_WANT_UTILS
#include "../WDL/jnetlib/webserver.h"
#include "../WDL/dirscan.h"
#include "../WDL/wdlcstring.h"

#include <sys/stat.h>

int g_config_cache_expire = 3600;
char g_wwwroot[2048];

static int hex_parse(char c)
{
  if (c >= '0' && c <= '9') return c-'0';
  if (c >= 'A' && c <= 'F') return 10+c-'A';
  if (c >= 'a' && c <= 'f') return 10+c-'a';
  return -1;
}
static WDL_UINT64 parseUInt64(const char *p, int *nc)
{
  WDL_UINT64 ret=0;
  while (*p)
  {
    char c = *p++;
    if (c < '0' || c > '9') break;
    ret *= 10;
    ret += c - '0';
    if (nc) (*nc)++;
  }
  return ret;
}
static void fmtUInt64(WDL_UINT64 in, char *op, int opSize)
{
  char tmp[128];
  char *outbuf = tmp + 32;
  *outbuf = 0;
  do
  {
    *--outbuf = '0' + (char)(in%10);
    in/=10;
  }
  while (in && outbuf > tmp);
  
  lstrcpyn_safe(op,outbuf,opSize);
}

void ProcessCommand(HOBO_Server *server, WDL_FastString *strout, const char *req, int reqLen)
{
  char want[1024];
  if (reqLen > sizeof(want)-1) reqLen = sizeof(want)-1;
  lstrcpyn_safe(want,req,reqLen+1);
  if (!strcmp(want,"CL"))
  {
    int x;
    strout->Append("CL");
    for(x=0;x<server->m_worldcons.GetSize();x++)
    {
      HOBO_ServerWorldCon *wc=server->m_worldcons.Get(x);
      if (wc)
      {
        int i=0;
        HOBO_ChannelRec *cn;
        while ((cn=wc->m_channels.Get(i++)))
        {
          const char *nameptr=cn->name.Get();
          if (nameptr[0] && cn->active)
          {
            strout->Append("\t");
            strout->Append(nameptr);
            char tmp[64];
            tmp[0]='\t';
            fmtUInt64(cn->m_hist_startidx + cn->hist.GetSize(),tmp+1,sizeof(tmp)-1);
            strout->Append(tmp);
          }
        }
      }
    }
    strout->Append("\n");
    return;
  }
  if (!strncmp(want,"CD/",3)) 
  {
    char *cname = want + 3;
    char *p = cname;
    while (*p && *p != '/') p++;
    if (p > want+3 && *p && p < want + 100)
    {
      *p++=0;
      int nc=0;
      WDL_UINT64 idx = parseUInt64(p,&nc);
      if (nc>0)
      {
        int maxlines = 0;
        if (p[nc] == '/') maxlines = atoi(p+nc+1);

        int x;
        for(x=0;x<server->m_worldcons.GetSize();x++)
        {
          HOBO_ServerWorldCon *wc=server->m_worldcons.Get(x);
          if (wc)
          {
            int i=0;
            HOBO_ChannelRec *cn;
            while ((cn=wc->m_channels.Get(i++)))
            {
              const char *nameptr=cn->name.Get();
              if (*cname == '.') *cname='#';
              if (cn->active && !stricmp(nameptr, cname))
              {
                WDL_INT64 aa = idx - cn->m_hist_startidx;
                if (aa<0) aa=0;
                int a = (int)aa;
                if (a<0) a=0;

                strout->Append("CDS\t");
                strout->Append(nameptr);

                char tmp[64];
                tmp[0]='\t';
                fmtUInt64(cn->m_hist_startidx + a,tmp+1,sizeof(tmp)-1);
                strout->Append(tmp);

                for (;a < cn->hist.GetSize(); a++)
                {
                  WDL_String *s = cn->hist.Get(a);
                  strout->Append("\nCD\t");
                  strout->Append(s->Get());
                  if (--maxlines == 0) break;
                }
                strout->Append("\n");

                return;
              }
            }
          }
        }
      }
    }
    return;
  }
  if (!strncmp(want,"SEND/",5))
  {
    char *p = want+5;
    while (*p && *p != '/') p++;
    if (*p && p>want+5 && p < want + 100)
    {
      int a = atoi(want+5);
      p++;
      if (*p)
      {
        HOBO_ServerWorldCon *wc=server->m_worldcons.Get(a);
        if (wc)
        {
          char buf[500];
          int x,l=strlen(p)/2;
          if (l>499) l=499;
          for(x=0;x<l;x++)
          {
            unsigned char cc = (hex_parse(p[x*2])<<4) + hex_parse(p[x*2+1]);
            buf[x] = (char)cc;
          }
          buf[x]=0;
          wc->SendMsg("%.200s",buf);
          wc->OnOutMessage(buf);
        }
      }
    }
  }

}

class wwwServer : public WebServerBaseClass
{
public:
  wwwServer(WDL_PtrList<HOBO_Server> *serverList) { m_servers=serverList; tmpgen=NULL; } 
  ~wwwServer() { delete tmpgen; }
  JNL_StringPageGenerator *tmpgen;
  virtual IPageGenerator *onConnection(JNL_HTTPServ *serv, int port)
  {
    serv->set_reply_header("Server:hoboprox/0.0");

    HOBO_Server *server=NULL;
    {
      const char *auth = serv->getheader("Authorization");
      if (auth)
      {
        if (!strnicmp(auth,"Basic",5))
        {
          auth += 5;
          while (*auth == ' ' || *auth == '\t') auth++;
          char decbuf[512];
          decbuf[0]=0;
          base64decode(auth,decbuf,sizeof(decbuf));

          int x;
          for(x=0;x<m_servers->GetSize() && !server;x++)
          {
            const char *np=m_servers->Get(x)->m_name.Get();
            int l = strlen(np);
            if (!strncmp(decbuf,np,l) && decbuf[l]==':' && !strcmp(decbuf+l+1,m_servers->Get(x)->m_password.Get()))
            {
              server = m_servers->Get(x);
            }
          }
        }
      } 
    }
    if (!server)
    {
      serv->set_reply_header("WWW-Authenticate: Basic realm=\"hoboprox\"");
      serv->set_reply_string("HTTP/1.1 401 Unauthorized");
      serv->set_reply_size(0);
      serv->send_reply();
      return 0;
    }
    const char *req = serv->get_request_file();
    if (!strncmp(req,"/_/",3))
    {
      req+=3;
      if (!tmpgen) tmpgen = new JNL_StringPageGenerator;

      while (*req == ';') req++;
      while (*req)
      {
        const char *np = req;
        while (*np && *np != ';') np++;
      
        ProcessCommand(server,&tmpgen->str,req,np-req);

        req = np;
        while (*req == ';') req++;
      }

      JNL_StringPageGenerator *res=NULL;
      if (tmpgen->str.Get()[0])
      {
        res=tmpgen;
        tmpgen=0;
      }

      serv->set_reply_header("Content-Type: text/plain");
      serv->set_reply_header("Cache-Control: no-cache, must-revalidate"); // HTTP/1.1
      serv->set_reply_header("Expires: Sat, 26 Jul 1997 05:00:00 GMT"); // Date in the past
      serv->set_reply_string("HTTP/1.1 200 OK");
      serv->set_reply_size(res ? strlen(res->str.Get()) : 0);
      serv->send_reply();
      return res;
    }
    else if (req[0] == '/' && !strstr(req,".."))
    {
      char buf[4096];
      lstrcpyn_safe(buf,g_wwwroot,1024);
      lstrcpyn_safe(buf+strlen(buf),req,1024);
      if (buf[strlen(buf)-1]=='/') strcat(buf,"index.html");
      //OutputDebugString(buf);
      struct stat s;
      if (!stat(buf,&s))
      {
        if ((s.st_mode & S_IFMT) == S_IFDIR) strcat(buf,"/index.html");

        WDL_FileRead *fr = new WDL_FileRead(buf,0,32768,2);
        if (fr->IsOpen())
        {
          char ctstr[512];
          strcpy(ctstr,"Content-Type: ");
          JNL_get_mime_type_for_file(buf,ctstr+strlen(ctstr),256);
          serv->set_reply_header(ctstr);
        
          if (g_config_cache_expire)
          {
            char timefmt[512];
            strcpy(timefmt,"Expires: ");
            char *p = timefmt+strlen(timefmt);
            JNL_Format_RFC1123(time(NULL)+g_config_cache_expire,p);
            if (p[0]) serv->set_reply_header(timefmt);
          }
          else
          {
            serv->set_reply_header("Cache-Control: no-cache, must-revalidate"); // HTTP/1.1
            serv->set_reply_header("Expires: Sat, 26 Jul 1997 05:00:00 GMT"); // Date in the past
          }

          serv->set_reply_string("HTTP/1.1 200 OK");
          serv->set_reply_size((int)fr->GetSize());
          serv->send_reply();
          return new JNL_FilePageGenerator(fr);
        }
        delete fr;
      }
    }

    serv->set_reply_string("HTTP/1.1 404 NOT FOUND");
    serv->set_reply_size(0);
    serv->send_reply();
    return 0; // no data
  }

  WDL_PtrList<HOBO_Server> *m_servers;
};

wwwServer *g_server;

bool HOBO_wwwRun(WDL_PtrList<HOBO_Server> *servers, JNL_IConnection *con, int port)
{
  if (!g_server && !con) return false;
  if (!g_server) 
  {
    if (!g_wwwroot[0]) strcpy(g_wwwroot,"./www_root");
    g_server=new wwwServer(servers);
  }
  if (con) g_server->attachConnection(con,port);
  g_server->run();
  return true;
}
