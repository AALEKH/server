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
#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation
#endif

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
#include "ha_recachesql.h"


static const char* csv_ext[]= {".txt",0};

/* A callback for the handlerton descriptor */
static handler *recachesql_create_handler(TABLE_SHARE *table);

static const char recachesql_hton_name[]= "RECACHESQL_csv";

static const char recachesql_hton_comment[]=
  "Simple read-only csv file storage engine";


handlerton recachesql_hton= {
  MYSQL_HANDLERTON_INTERFACE_VERSION,
  recachesql_hton_name,
  SHOW_OPTION_YES,
  recachesql_hton_comment,
  DB_TYPE_BLACKHOLE_DB,
  NULL,
  0,       /* slot */
  0,       /* savepoint size. */
  NULL,    /* close_connection */
  NULL,    /* savepoint */
  NULL,    /* rollback to savepoint */
  NULL,    /* release savepoint */
  NULL,    /* commit */
  NULL,    /* rollback */
  NULL,    /* prepare */
  NULL,    /* recover */
  NULL,    /* commit_by_xid */
  NULL,    /* rollback_by_xid */
  NULL,    /* create_cursor_read_view */
  NULL,    /* set_cursor_read_view */
  NULL,    /* close_cursor_read_view */
  recachesql_create_handler,    /* Create a new handler */
  NULL,    /* Drop a database */
  NULL,    /* Panic call */
  NULL,    /* Start Consistent Snapshot */
  NULL,    /* Flush logs */
  NULL,    /* Show status */
  NULL,    /* Partition flags */
  NULL,    /* Alter table flags */
  NULL,    /* Alter Tablespace */
  NULL,    /* Fill FILES table */
  HTON_CAN_RECREATE | HTON_ALTER_CANNOT_CREATE,
  NULL,
  NULL,
  NULL
};

static handler *recachesql_create_handler(TABLE_SHARE *table)
{
  return new ha_recachesql(table);
}

ha_recachesql::ha_recachesql(TABLE_SHARE *table_arg)
  :handler(&recachesql_hton, table_arg)
{}


int ha_recachesql::open(const char *name, int mode, uint test_if_locked)
{
  /* Initialize the lock structures used by the lock manager. */
  thr_lock_init(&thr_lock);
  thr_lock_data_init(&thr_lock,&lock,NULL);
  /* Allocate memory for the data file descriptor. */
  file= (csv_INFO*)my_malloc(sizeof(csv_INFO),MYF(MY_WME));
  if (!file)
    return 1;
  /* Translate the name of the name into the datafile name. */
  fn_format(file->fname, name, "", ".txt", MY_REPLACE_EXT|MY_UNPACK_FILENAME);
  /*
     Open the file, and save the file handle id in the data
     file descriptor structure.
   */
  if ((file->fd = my_open(file->fname,mode,MYF(0))) < 0) {
    int error = my_errno; close( );
    return error;
  }
  /* Read operations start from the beginning of the file. */
  pos = 0;
  return 0;
}


int ha_recachesql::close(void)
{
  thr_lock_delete(&thr_lock);
  if (file) {
    if (file->fd >= 0)
      my_close(file->fd, MYF(0));
    my_free((gptr)file,MYF(0));
    file = 0;
  }
  return 0;
}


int ha_recachesql::fetch_line(byte* buf) {
   my_off_t cur_pos = pos;
   Field** field = table->field;
   int last_c = 256;
   int in_quote = 0;
   uint bytes_parsed = 0;
   int line_read_done = 0;
   field_buf.length(0);
   for (;!line_read_done;)
    char buf[csv_READ_BLOCK_SIZE];
    uint bytes_read = my_pread(file->fd,buf,sizeof(buf),cur_pos,MYF(MY_WME));
    if (bytes_read == MY_FILE_ERROR)
      return HA_ERR_END_OF_FILE;
    if (!bytes_read)
      return HA_ERR_END_OF_FILE;
    char* p = buf;
    char* buf_end = buf + bytes_read;
    for (;p < buf_end;)
    {
      char c = *p;
      int end_of_line = 0;
      int end_of_field = 0;
      int char_escaped = 0;
      switch (c) {
        case '"':
          if (last_c == '"' || last_c == '\\') {
            field_buf.append(c);
            char_escaped = 1;

            if (last_c == '"')
              in_quote = 1;
          }
          else
            in_quote = !in_quote;
          break;
        case '\\':
          if (last_c == '\\')  {
             field_buf.append(c);
             char_escaped = 1;
          }
          break;
         case '\r':
         case '\n':
          if (in_quote) {
            field_buf.append(c);
          }
          else {
            end_of_line = 1;
            end_of_field = 1;
          }
          break;
        case ',':
          if (in_quote) {
            field_buf.append(c);
          }
          else
            end_of_field = 1;
          break;
        default:
            field_buf.append(c);
          break;
      }

      if (end_of_field && *field) {
        (*field)->store(field_buf.ptr(),field_buf.length( ), system_charset_info);
        field++;
        field_buf.length(0);
      }

      if (char_escaped)
        last_c = 256;
      else
        last_c = c;
      p++;

      if (end_of_line)
      {
        if (c == '\r')
          p++;
        line_read_done = 1;
        in_quote = 0;
        break;
      }
    }
    bytes_parsed += (p - buf);
    cur_pos += bytes_read;
  }
  memset(buf,0,table->s->null_bytes);

  for (;*field;field++) {
    (*field)->set_default( );
  }
  pos += bytes_parsed;
  return 0;
}

int ha_recachesql::rnd_next(byte *buf)
{

  statistic_increment(ha_read_rnd_next_count,&LOCK_status);
  int error = fetch_line(buf);
  if (!error)
    records++;
  return error;
}

int ha_recachesql::rnd_next(byte *buf)
{

  ha_statistic_increment(&SSV::ha_read_rnd_next_count);

  if (!error)
    records++;
  return error;
}

int ha_recachesql::rnd_pos(byte * buf, byte *set_pos)
{
  ha_statistic_increment(&SSV::ha_read_rnd_count);
  pos = my_get_ptr(set_pos,ref_length);
  return fetch_line(buf);
}

void ha_recachesql::position(const byte *record)
{
  my_store_ptr(ref,ref_length,pos);
}

void ha_recachesql::info(uint flags)
{

  if (records < 2)
    records = 2;

  deleted = 0;
  errkey = 0;
  mean_rec_length = 0;
  data_file_length = 0;
  index_file_length = 0;
  max_data_file_length = 0;
  delete_length = 0;
  if (flags & HA_STATUS_AUTO)
    auto_increment_value = 1;
}

int ha_recachesql::external_lock(THD *thd, int lock_type){
return 0;
}

const char ** ha_recachesql::bas_ext() const {
  return csv_ext;
}

ulong ha_recachesql::table_flags(void) const
{
  return HA_NOT_EXACT_COUNT;
}

ulong ha_recachesql::index_flags(uint idx, uint part, bool all_parts) const
{
return 0;
}

int ha_recachesql::create(const char *name, TABLE *form, HA_CREATE_INFO *info)
{
return 0;
}

/* This method is needed for the table lock manager to work right. */
THR_LOCK_DATA ** ha_recachesql::store_lock(THD *thd,
            THR_LOCK_DATA **to,
            enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type=lock_type;
  *to++ = &lock;
return to; }
/* Defines the global structure for the plug-in. */
mysql_declare_plugin(recachesql_csv)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &recachsql_hton,
  recachesql_hton_name,
   "Aalekh Nigam",
  recachsql_hton_comment,
  NULL, /* Plugin init function */
  NULL, /* Plugin end function */
  0x0100,
  0
}
mysql_declare_plugin_end;
