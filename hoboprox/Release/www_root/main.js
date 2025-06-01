function initxmlhttp()
{
var xmlhttp=false;
/*@cc_on @*/
/*@if (@_jscript_version >= 5)
// JScript gives us Conditional compilation, we can cope with old IE versions.
// and security blocked creation of the objects.
 try {
  xmlhttp = new ActiveXObject("Msxml2.XMLHTTP");
 } catch (e) {
  try {
   xmlhttp = new ActiveXObject("Microsoft.XMLHTTP");
  } catch (E) {
   xmlhttp = false;
  }
 }
@end @*/
  if (!xmlhttp && typeof XMLHttpRequest!='undefined')
  {
        try {
                xmlhttp = new XMLHttpRequest();
        } catch (e) {
                xmlhttp=false;
        }
  }
  if (!xmlhttp && window.createRequest)
  {
        try {
                xmlhttp = window.createRequest();
        } catch (e) {
                xmlhttp=false;
        }
  }
  return xmlhttp;
}


var g_wwr_timer_freq=100;
var g_wwr_req_list = "";
var g_wwr_req_recur = new Array();
var g_wwr_req = null;
var g_wwr_timer = null;
var g_wwr_timer2 = null;
function wwr_run_update()
{
  g_wwr_timer = null;
  if (!g_wwr_req) g_wwr_req = initxmlhttp();
  if (!g_wwr_req) 
  {
    alert("no xml http support");
    return;
  }

  if (g_wwr_req_list == "")
  {
    runNewRequests();
  }

  // populate any periodic requests
  var x;
  var d = (new Date).getTime();
  for (x=0;x<g_wwr_req_recur.length;x++)
  {
    if (g_wwr_req_recur[x][2] < d)
    {
      g_wwr_req_recur[x][2] = d + g_wwr_req_recur[x][1];
      g_wwr_req_list += g_wwr_req_recur[x][0] + ";";
    }
  }

  if (g_wwr_req_list != "")
  {
    g_wwr_req.open("GET","/_/" + g_wwr_req_list,true);
    g_wwr_req.onreadystatechange=function() 
    {
      if (g_wwr_req.readyState==4) 
      {
        if (g_wwr_timer2) { clearTimeout(g_wwr_timer2); g_wwr_timer2=null; }
        wwr_onreply(g_wwr_req.responseText);
        wwr_run_update();
      }
    };

    if (g_wwr_timer2) clearTimeout(g_wwr_timer2); 
    g_wwr_timer2 = window.setTimeout(function()
    {
      g_wwr_timer2=null;
      if (g_wwr_req.readyState!=0 && g_wwr_req.readyState!=4)
      {
         if (g_wwr_timer) { clearTimeout(g_wwr_timer); g_wwr_timer=null; }
         g_wwr_req.abort();
         wwr_run_update();
      }
    },3000);
    
    g_wwr_req_list = "";
    g_wwr_req.send(null);
  }
  else
  {
    g_wwr_timer = window.setTimeout(wwr_run_update,g_wwr_timer_freq);
  }
}


function wwr_start()
{
   wwr_run_update();
}
function wwr_req(name)
{
  g_wwr_req_list += name + ";";
}

function wwr_req_recur(name, interval)
{
  g_wwr_req_recur.push([name,interval,0]);
}
