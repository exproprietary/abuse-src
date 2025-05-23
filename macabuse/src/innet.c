/*

  This file is a combination of :
    src/net/unix/unixnfc.c 
    src/net/unix/netdrv.c 
    src/net/unix/undrv.c

    netdrv & undrv compile to a stand-alone program with talk with unixnfc
via a FIFO in /tmp, using a RPC-like scheme.  This versions runs inside
of a abuse and therefore is a bit simpler.


*/
#include "idle.hpp"
#include "demo.hpp"
#include "macs.hpp"
#include "specs.hpp"
#include "level.hpp"
#include "game.hpp"
#include <stdio.h>
#include "timing.hpp"
#include "fileman.hpp"
#include "netface.hpp"

#include "gserver.hpp"
#include "gclient.hpp"
#include "dprint.hpp"
#include "netcfg.hpp"


extern char *symbol_str(char *name);

#ifdef __WATCOMC__
#define getlogin() "DOS user"
#endif


base_memory_struct *base;   // points to shm_addr
base_memory_struct local_base;
net_address *net_server=NULL;
extern int registered;
net_protocol *prot=NULL;
net_socket *comm_sock=NULL,*game_sock=NULL;
extern char lsf[256];
game_handler *game_face=NULL;
int local_client_number=0;        // 0 is the server
join_struct *join_array=NULL;      // points to an array of possible joining clients
extern char *get_login();
extern void set_login(char *name);

int net_init(int argc, char **argv)
{
  int i,x,db_level=0;
  base=&local_base;

  local_client_number=0;

  if (!main_net_cfg)
    main_net_cfg=new net_configuration;


  if (!registered) return 0;

  for (i=1;i<argc;i++) 
    if (!strcmp(argv[i],"-nonet"))
      return 0;
    else if (!strcmp(argv[i],"-port"))
    {
      if (i==argc-1 || !sscanf(argv[i+1],"%d",&x) || x<1 || x>0x7fff)
      {
        dprintf("bad value following -port, use 1..32000\n");
        return 0;
      } 
      else 
        main_net_cfg->port=x;
    } else if (!strcmp(argv[i],"-net") && i<argc-1)
    {
      i++;
      strcpy(main_net_cfg->server_name,argv[i]);
      main_net_cfg->state=net_configuration::CLIENT;
    }
    else if (!strcmp(argv[i],"-ndb"))
    {
      if (i==argc-1 || !sscanf(argv[i+1],"%d",&x) || x<1 || x>3)
      {
        dprintf("bad value following -ndb, use 1..3\n");
        return 0;
      } else { db_level=x; }
    } else if (!strcmp(argv[i],"-server"))
      main_net_cfg->state=net_configuration::SERVER; 
    else if (!strcmp(argv[i],"-min_players"))
    {
      i++; 
      int x=atoi(argv[i]);
      if (x>=1 && x<=8)
        main_net_cfg->min_players=x;
      else dprintf("bad value for min_players use 1..8\n");
    }

  
  net_protocol *n=net_protocol::first,*usable=NULL;     // find a usable protocol from installed list
  int total_usable=0;
  for (;n;n=n->next)                                    // show a list of usables, just to be cute
  {
    dprintf("Protocol %s : ",n->installed() ? "Installed" : "Not_installed");
    dprintf("%s\n",n->name());
    if (n->installed()) 
    {
      total_usable++;
      usable=n;
    }      
  }

  if (!usable) { dprintf("No network protocols installed\n");  return 0; }
  prot=usable;
  prot->set_debug_printing((net_protocol::debug_type)db_level);
  if (main_net_cfg->state==net_configuration::SERVER)
    set_login(main_net_cfg->name);

  comm_sock=game_sock=NULL;
  if (main_net_cfg->state==net_configuration::CLIENT)
  {
    dprintf("Attempting to locate server %s, please wait\n",main_net_cfg->server_name);
    char *sn=main_net_cfg->server_name;
    if (main_net_cfg->addr)
      net_server = main_net_cfg->addr->copy();
    else
      net_server=prot->get_node_address(sn,DEFAULT_COMM_PORT,0);
    if (!net_server) { dprintf(symbol_str("unable_locate"));  exit(0); }   
    dprintf("Server located!  Please wait while data loads....\n");
  }

  fman=new file_manager(argc,argv,prot);                                       // manages remote file access
  if (game_face)
    delete game_face;
  game_face=new game_handler;
  join_array=(join_struct *)jmalloc(sizeof(join_struct)*MAX_JOINERS,"join array");
  base->join_list=NULL;
  base->mem_lock=0;
  base->calc_crcs=0;
  base->get_lsf=0;
  base->wait_reload=0;
  base->need_reload=0;
  base->input_state=INPUT_COLLECTING;
  base->current_tick=0;
  base->packet.packet_reset();

  return 1;
}

 


int net_start()  // is the game starting up off the net? (i.e. -net hostname)
{   return (main_net_cfg && main_net_cfg->state==net_configuration::CLIENT);  }



int kill_net()
{ 
  if (game_face) delete game_face;  
  game_face=new game_handler;

  if (join_array) jfree(join_array);  join_array=NULL;
  if (game_sock) { delete game_sock; game_sock=NULL; }
  if (comm_sock) { delete comm_sock; comm_sock=NULL; }
  delete fman;  fman=NULL;
  if (net_server) { delete net_server; net_server=NULL; }
  if (prot) 
  { 
  
    prot->cleanup(); 
    prot=NULL; 
    return 1;
  } else return 0;
}

void net_uninit()
{
  kill_net();
}


int NF_set_file_server(net_address *addr)
{
  if (prot)
  {
    fman->set_default_fs(addr);
    net_socket *sock=prot->connect_to_server(addr,net_socket::SOCKET_SECURE);

    if (!sock) { dprintf("set_file_server::connect failed\n"); return 0; }
    uchar cmd=CLIENT_CRC_WAITER;
    if ( sock->write(&cmd,1)!=1 )
      dprintf("set_file_server::writefailed\n");
    else
      if (sock->read(&cmd,1)!=1)
      {
        dprintf("set_file_server::read failed\n");        // wait for confirmation that crc's are written
        delete sock; 
        return 0; 
      }
    delete sock;
    return cmd; 
  } else return 0;
}

int NF_set_file_server(char *name)
{
  if (prot)
  {
    net_address *addr=prot->get_node_address(name,DEFAULT_COMM_PORT,0);
    if (addr)
    {
      int ret=NF_set_file_server(addr);
      delete addr;
      return ret;
    } else return 0;
  } else return 0;
}


int NF_open_file(char *filename, char *mode)
{
  if (prot)
    return fman->rf_open_file(filename,mode);
  else return -2;
}


long NF_close(int fd)
{
  if (prot)
    return fman->rf_close(fd);
  else return 0;
}

long NF_read(int fd, void *buf, long size)
{
  if (prot)
    return fman->rf_read(fd,buf,size);
  else return 0;
}

long NF_filelength(int fd)
{
  if (prot)
    return fman->rf_file_size(fd);
  else return 0;
}

long NF_seek(int fd, long offset)
{
  if (prot)
    return fman->rf_seek(fd,offset);
  else return 0;
}

long NF_tell(int fd)
{
  if (prot)
    return fman->rf_tell(fd);
  else return 0;
}


void service_net_request() 
{
  if (prot)
  {
    if (prot->select(0))  // anything happening net-wise?
    {
      if (comm_sock && comm_sock->ready_to_read())  // new connection?
      {
        net_address *addr;
			
        net_socket *new_sock=comm_sock->accept(addr);	
        if (new_sock)
        {
          uchar client_type;
          if (new_sock->read(&client_type,1)!=1)
          {
            delete addr;	
            delete new_sock;
          }
          else
          {
            switch (client_type)
            {
              case CLIENT_NFS :
              {
                delete addr;	
                fman->add_nfs_client(new_sock);
              } break;
              case CLIENT_CRC_WAITER :
              {		
                crc_man.write_crc_file(NET_CRC_FILENAME);       // return 0 on failure
                client_type=1;                                  // confirmation byte
                new_sock->write(&client_type,1);
                delete new_sock;                                // done with this socket now
                delete addr;	
              } break;
              case CLIENT_LSF_WAITER :          // wants to know which .lsp file to start with
              {
                uchar len=strlen(lsf);
                new_sock->write(&len,1);
                new_sock->write(lsf,len);
                delete new_sock;
                delete addr;
              } break;
              default :
              {
                if (!game_face || game_face->add_client(client_type,new_sock,addr)==0)  // ask server or client to add new client
                {
                  delete addr;
                  delete new_sock;
                }
              } break;
            }
          }		    
        }
      }
      if (game_face && !game_face->process_net())
      {
        delete game_face;
        game_face=new game_handler;
      }
      fman->process_net();      
    }
  }
}


int get_remote_lsf(net_address *addr, char *filename)  // filename should be 256 bytes
{
  if (prot)
  {
    net_socket *sock=prot->connect_to_server(addr,net_socket::SOCKET_SECURE);
    if (!sock) return 0;

    uchar ctype=CLIENT_LSF_WAITER;
    uchar len;

    if (sock->write(&ctype,1)!=1 ||
        sock->read(&len,1)!=1 || len==0 ||
        sock->read(filename,len)!=len)
    {
      delete sock; 
      return 0;
    } 

    delete sock;
    return 1;  

  } else return 0;
}

void server_check() { ; } 

int request_server_entry()
{
  if (prot && main_net_cfg)
  {
    if (!net_server) return 0;

    if (game_sock) delete game_sock;
    dprintf("Joining game in progress, hang on....\n");

    int game_port = main_net_cfg->game_port;
		
    game_sock=prot->create_listen_socket(game_port,net_socket::SOCKET_FAST);     // this is used for fast game packet transmission
    if (!game_sock) { if (comm_sock) delete comm_sock; comm_sock=NULL; prot=NULL; return 0; }
    game_sock->read_selectable();
    main_net_cfg->game_port = game_port;

    net_socket *sock=prot->connect_to_server(net_server,net_socket::SOCKET_SECURE);
    if (!sock)
    { 
      dprintf("unable to connect to server\n");
      return 0;
    }

    uchar ctype=CLIENT_ABUSE;
    ushort port=lstl(game_port),cnum;

    uchar reg;
    if (sock->write(&ctype,1)!=1 ||   // send server our game port
        sock->read(&reg,1)!=1)        // is remote engine registered?
    { delete sock; return 0; }

    if (reg==2)   // too many players
    {
      dprintf(symbol_str("max_players"));
      delete sock;
      return 0;
    }

    // maker sure the two games are both registered or unregistered or sync problems
    // will occur.

    if (reg && !registered)
    {
      dprintf(symbol_str("net_not_reg"));
      delete sock; 
      return 0;
    } 

    if (!reg && registered)
    {
      dprintf(symbol_str("server_not_reg"));
      delete sock;
      return 0;
    }

    char uname[256];
    if (get_login())
      strcpy(uname,get_login());
    else strcpy(uname,"unknown");
    uchar len=strlen(uname)+1;
    short nkills;

    if (sock->write(&len,1)!=1 ||
        sock->write(uname,len)!=len || 
        sock->write(&port,2)!=2  ||            // send server our game port
        sock->read(&port,2)!=2   ||            // read server's game port
        sock->read(&nkills,2)!=2 ||
        sock->read(&cnum,2)!=2   || cnum==0    // read player number (cannot be 0 because 0 is server)
        )
    { delete sock; return 0; }

    nkills=lstl(nkills);
    port=lstl(port);
    cnum=lstl(cnum);
    
    main_net_cfg->kills=nkills;
    net_address *addr=net_server->copy();
    addr->set_port(port);

    if (game_face)
      delete game_face;
    game_face=new game_client(sock,addr);
    delete addr;

    local_client_number=cnum;
    return cnum;
  } else return 0;
}

int reload_start()
{
  if (prot && game_face)
    return game_face->start_reload();
  else return 0;
}

int reload_end()
{
  if (prot && game_face)
    return game_face->end_reload();
  else return 0;
}


void net_reload()
{
  if (prot)
  {
    if (net_server)
    {
      if (current_level)
        delete current_level;
      bFILE *fp;

      if (!reload_start()) return ;

      do {            // make sure server saves the file
        fp=open_file(NET_STARTFILE,"rb");
        if (fp->open_failure()) { delete fp; fp=NULL; }
      } while (!fp);

      spec_directory sd(fp);  

      spec_entry *e=sd.find("Copyright 1995 Crack dot Com, All Rights reserved"); 
      if (!e)
      { 
        the_game->show_help("This level is missing copyright information, cannot load\n");
        current_level=new level(100,100,"untitled");
        the_game->need_refresh();
      }
      else 
        current_level=new level(&sd,fp,NET_STARTFILE);

      delete fp;     
      base->current_tick=(current_level->tick_counter()&0xff); 

      reload_end();
    } else if (current_level)
    {
      
      join_struct *join_list=base->join_list;


      while (join_list)
      {
	
        view *f=player_list;
        for (;f && f->next;f=f->next);      // find last player, add one for pn
        int i,st=0;
        for (i=0;i<total_objects;i++)
          if (!strcmp(object_names[i],"START"))
            st=i;
			
        game_object *o=create(current_start_type,0,0);
        game_object *start=current_level->get_random_start(320,NULL);
        if (start) { o->x=start->x; o->y=start->y; }
        else { o->x=100; o->y=100; }
			
        f->next=new view(o,NULL,join_list->client_id);
        strcpy(f->next->name,join_list->name);
        o->set_controller(f->next);
			
        if (start)
          current_level->add_object_after(o,start);
        else
          current_level->add_object(o);
			
        view *v=f->next;      
			
        join_list=join_list->next;
      }     
      base->join_list=NULL;
      current_level->save(NET_STARTFILE,1);
      base->mem_lock=0;


      jwindow *j=eh->new_window(0,yres/2,-1,-1,new info_field(WINDOW_FRAME_LEFT,
          WINDOW_FRAME_TOP,
          0,symbol_str("resync"),
          new button(WINDOW_FRAME_LEFT,
              WINDOW_FRAME_TOP+eh->font()->height()+5,ID_NET_DISCONNECT,
              symbol_str("slack"),NULL)),symbol_str("hold!"))
        ;

  

      eh->flush_screen();
      if (!reload_start()) return ;

      base->input_state=INPUT_RELOAD;    // if someone missed the game tick with the RELOAD data in it, make sure the get it
  
      // wait for all client to reload the level with the new players
      do  
      { 
        service_net_request();
        if (eh->event_waiting())
        {
          event ev;
          do
          {           
            eh->get_event(ev);
            if (ev.type==EV_MESSAGE && ev.message.id==ID_NET_DISCONNECT)
            {
              if (game_face)
                game_face->end_reload(1);
              base->input_state=INPUT_PROCESSING; 
            }
			
          } while (eh->event_waiting()); 
			
          eh->flush_screen();
        } else 
        {
          if (idle_man)
            idle_man->idle();
        }

      } while (!reload_end());  
      eh->close_window(j);
      unlink(NET_STARTFILE);

      the_game->reset_keymap();

      base->input_state=INPUT_COLLECTING;

    }     
  }
}


int client_number() { return local_client_number; }


void send_local_request()
{

  if (prot)
  {    
    if (current_level)
      base->current_tick=(current_level->tick_counter()&0xff); 
    if (game_face)
      game_face->add_engine_input();
  } else base->input_state=INPUT_PROCESSING;

}


void kill_slackers()
{
  if (prot)
  {
    if (game_face && !game_face->kill_slackers())
    {
      delete game_face;
      game_face=new game_handler();
    }
  }
}

int get_inputs_from_server(unsigned char *buf)
{
  if (idle_man)
    idle_man->idle_reset();

  if (prot && base->input_state!=INPUT_PROCESSING)      // if input is not here, wait on it
  {
    time_marker start;

    int total_retry=0;
    jwindow *abort=NULL;

    while (base->input_state!=INPUT_PROCESSING)
    { 
      if (!prot)
      { 
        base->input_state=INPUT_PROCESSING; 
        return 1; 
      }
      service_net_request();

      time_marker now;                   // if this is taking to long, the packet was probably lost, ask for it to be resent

      if (now.diff_time(&start)>0.05)
      {
        if (idle_man)
          idle_man->idle();
        if (prot->debug_level(net_protocol::DB_IMPORTANT_EVENT))
          dprintf("(missed packet)");
			
        if (game_face)
          game_face->input_missing();
        start.get_time();
			
        total_retry++;
        if (total_retry==12000)    // 2 minutes and nothing
        {
          abort=eh->new_window(0,yres/2,-1,eh->font()->height()*4,
              new info_field(WINDOW_FRAME_LEFT,
                  WINDOW_FRAME_TOP,
                  0,symbol_str("waiting"),
                  new button(WINDOW_FRAME_LEFT,
                      WINDOW_FRAME_TOP+eh->font()->height()+5,ID_NET_DISCONNECT,
                      symbol_str("slack"),NULL)),symbol_str("Error"));	  
          eh->flush_screen();
        }
      }
      if (abort)
      {
        if (eh->event_waiting())
        {
          event ev;
          do
          {
            eh->get_event(ev);
            if (ev.type==EV_MESSAGE && ev.message.id==ID_NET_DISCONNECT)
            {
              kill_slackers();
              base->input_state=INPUT_PROCESSING; 
            }
          } while (eh->event_waiting());
			
          eh->flush_screen();
        }
      }
    }

    if (abort)
    {
      eh->close_window(abort);
      the_game->reset_keymap();

    }
  }


  memcpy(base->last_packet.data,base->packet.data,base->packet.packet_size()+base->packet.packet_prefix_size());
  
  int size=base->packet.packet_size();  
  memcpy(buf,base->packet.packet_data(),size);

  base->packet.packet_reset();
  base->mem_lock=0;

  return size;
}

int become_server(char *name)
{
  if (prot && main_net_cfg)
  {

    if (comm_sock) delete comm_sock;
    
    int port = main_net_cfg->port, gameport = main_net_cfg->game_port;
    
    comm_sock=prot->create_listen_socket(port,net_socket::SOCKET_SECURE);   // this is used for incomming connections
    main_net_cfg->port = port;
 
    if (!comm_sock) { prot=NULL; return 0; }
    comm_sock->read_selectable();
    prot->start_notify(0x9090, name, strlen(name));  // should we define a new socket for notifiers?

    if (game_sock) delete game_sock;
    game_sock=prot->create_listen_socket(gameport,net_socket::SOCKET_FAST);     // this is used for fast game packet transmission
    if (!game_sock) { if (comm_sock) delete comm_sock; comm_sock=NULL; prot=NULL; return 0; }
    game_sock->read_selectable();
    main_net_cfg->game_port = gameport;

    if (game_face)
      delete game_face;
    game_face=new game_server;
    local_client_number=0;
    return 1;
  }
  return 0;
}

void read_new_views() { ; }


void wait_min_players()
{
  if (game_face) 
    game_face->game_start_wait();
}
