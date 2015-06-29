/* Copyright (c) 2006, 2014, Oracle and/or its affiliates.
   Copyright (c) 2008, 2014, Monty Program Ab
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA */

#include <my_global.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <iostream>
#include <queue>
#include <thread>
#include <assert.h>
#include <mysql_version.h>
#include <mysql/plugin.h>
#include <my_dir.h>
#include "my_thread.h"
#include "my_sys.h"                             // my_write, my_malloc
#include "sql_plugin.h"                         // st_plugin_int

#include "cpp-hiredis-cluster/include/hirediscommand.h" //Redis Cluster library

using namespace RedisCluster;
using std::string;
using std::cout;
using std::cerr;
using std::endl;

PSI_memory_key key_memory_mysql_innodb_redis_context;

#ifdef HAVE_PSI_INTERFACE

static PSI_memory_info all_innodb_redis_memory[]=
{
  {&key_memory_mysql_innodb_redis_context, "mysql_innodb_redis_context", 0}
};

static void init_innodb_redis_psi_keys()
{
  const char* category= "innodb_redis";
  int count;

  count= array_elements(all_innodb_redis_memory);
  mysql_memory_register(category, all_innodb_redis_memory, count);
};
#endif /* HAVE_PSI_INTERFACE */


  
struct mysql_innodb_redis_context
{

};

void *mysql_innodb_redis(void *p)
{
  DBUG_ENTER("mysql_innodb_redis");
  /*
  InnoDB Redis main logic function
  */

  DBUG_RETURN(0);
}

/*
  Initialize the innodb redis at server start or plugin installation.

  SYNOPSIS
    innodb_redis_plugin_init()

  DESCRIPTION
    Starts up innodb redis thread

  RETURN VALUE
    0                    success
    1                    failure (cannot happen)
*/

static int innodb_redis_plugin_init(void *p)
{
  DBUG_ENTER("innodb_redis_plugin_init");

#ifdef HAVE_PSI_INTERFACE
  init_innodb_redis_psi_keys();
#endif

  /*
  InnoDB Redis Initialization function
  */
  system("redis-3.0/utils/create-cluster/create-six start");
  system("redis-3.0/utils/create-cluster/create-six create");
  plugin->data= (void *)con;

  DBUG_RETURN(0);
}


/*
  Terminate the innodb redis at server shutdown or plugin deinstallation.

  SYNOPSIS
    innodb_redis_plugin_deinit()
    Does nothing.

  RETURN VALUE
    0                    success
    1                    failure (cannot happen)

*/

static int innodb_redis_plugin_deinit(void *p)
{
  DBUG_ENTER("innodb_redis_plugin_deinit");

  // InnoDB Redis deinitialization plugin
  system("redis-3.0/utils/create-cluster/create-six stop");
  DBUG_RETURN(0);
}


struct st_mysql_daemon innodb_redis_plugin=
{ MYSQL_DAEMON_INTERFACE_VERSION  };

/*
  Plugin library descriptor
*/

mysql_declare_plugin(innodb_redis)
{
  MYSQL_DAEMON_PLUGIN,
  &innodb_redis_plugin,
  "innodb_redis",
  "Aalekh Nigam",
  "InnoDB Daemon Plugin, to provide Redis Interface to MariaDB",
  PLUGIN_LICENSE_GPL,
  innodb_redis_plugin_init, /* Plugin Init */
  innodb_redis_plugin_deinit, /* Plugin Deinit */
  0x0100 /* 1.0 */,
  NULL,                       /* status variables                */
  NULL,                       /* system variables                */
  NULL,                       /* config options                  */
  0,                          /* flags                           */
}
mysql_declare_plugin_end;