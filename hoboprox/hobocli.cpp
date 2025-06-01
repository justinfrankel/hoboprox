#include <stdio.h>


#include "hobo_hobocon.h"

#include "../WDL/rng.h"
#include "../WDL/wdltypes.h"


void messageCallback(int msgtype, const char *msg, int msglen, void *userData, HOBO_HoboCon *con)
{
  if (msgtype == HOBO_MESSAGE_NORMAL && msg)
  {
    char buf[1024];
    memcpy(buf,msg,min(1023,msglen));
    buf[min(1023,msglen)]=0;
    printf("msg '%s'\n",buf);
  }
  else if (msgtype == HOBO_MESSAGE_WARNING || msgtype == HOBO_MESSAGE_ERROR)
  {
    if (msglen < 0)
      printf("%s: %s\n",msgtype == HOBO_MESSAGE_WARNING?"WARNING":"ERROR",msg);
  }
  else printf("got message type %d\n",msgtype); 
}

int main(int argc, char **argv)
{
  JNL::open_socketlib();

  HOBO_HoboCon *hobocon = NULL;
  
  const int reconnect_interval = 15;
  time_t last_contime = 0;

  for (;;)
  {
    if (!hobocon) // not connected yet
    {
      if (time(NULL) > last_contime+reconnect_interval)
      {
        last_contime=time(NULL);
        JNL_Connection *precon = new JNL_Connection;
        precon->connect("127.0.0.1",6969);
        hobocon = new HOBO_HoboCon(false,precon,"lame","test");
      }
    }
    else
    {
      if (hobocon->Run(messageCallback,NULL))
      {
        printf("!!! Destroying hobocon\n");
        delete hobocon;
        hobocon=0;
      }
    }
    Sleep(1);
  }

  delete hobocon;

  JNL::close_socketlib();

  return 0;
}
