/* $Id: hash_drv.c,v 1.3 2005/09/24 17:48:59 jonz Exp $ */

/*
 DSPAM
 COPYRIGHT (C) 2002-2005 DEEP LOGIC INC.

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; version 2
 of the License.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/*
 * hash_drv.c - CRM114 sparse spectra storage driver
 *             mmap'd flat-file storage for fast markovian-based filtering
 *
 * DESCRIPTION
 *   This driver uses a flat, fixed-size file for storage. It is exceptionally
 *   fast and does not require any third-party dependencies. The default fixed
 *   size stores up to a million records.
 */

#ifdef HAVE_CONFIG_H
#include <auto-config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>

#ifdef TIME_WITH_SYS_TIME
#   include <sys/time.h>
#   include <time.h>
#else
#   ifdef HAVE_SYS_TIME_H
#       include <sys/time.h>
#   else
#       include <time.h>
#   endif
#endif

#include "storage_driver.h"
#include "config_shared.h"
#include "hash_drv.h"
#include "libdspam.h"
#include "config.h"
#include "error.h"
#include "language.h"
#include "util.h"

int
dspam_init_driver (DRIVER_CTX *DTX)
{
  DSPAM_CTX *CTX;
  char *HashConcurrentUser;

  if (DTX == NULL) 
    return 0;

  CTX = DTX->CTX;
  HashConcurrentUser 
    = _ds_read_attribute(CTX->config->attributes, "HashConcurrentUser");

#ifdef DAEMON
  if (DTX->flags & DRF_STATEFUL) {
    if (HashConcurrentUser) {
      char filename[MAX_FILENAME_LENGTH];
      hash_drv_map_t map;
      unsigned long hash_rec_max;
      int ret;

      /* Connection pointer array (just one for hash_drv) */
      DTX->connections = calloc(1, sizeof(struct _ds_drv_connection *));
      if (DTX->connections == NULL) 
        goto memerr;

      /* Our single connection */
      DTX->connections[0] = calloc(1, sizeof(struct _ds_drv_connection));
      if (DTX->connections[0] == NULL) 
        goto memerr;

      /* Our storage structure */
      DTX->connections[0]->dbh = calloc(1, sizeof(struct _hash_drv_map));
      if (DTX->connections[0]->dbh == NULL) 
        goto memerr;

      map = (hash_drv_map_t) DTX->connections[0]->dbh;

      if (!(DTX->flags & DRF_RWLOCK))
        DTX->flags |= DRF_RWLOCK;
      DTX->connection_cache = 1;

      if (_ds_read_attribute(DTX->CTX->config->attributes, "HashRecMax")) {
        hash_rec_max = strtol(_ds_read_attribute(DTX->CTX->config->attributes,
          "HashRecMax"), NULL, 0); 
      } else {
        hash_rec_max = HASH_REC_MAX;
      }
      
      _ds_userdir_path(filename, DTX->CTX->home, HashConcurrentUser, "css");
      _ds_prepare_path_for(filename);
      ret = _hash_drv_open(filename, map, hash_rec_max); 
      if (ret) {
        LOG(LOG_CRIT, "_hash_drv_open(%s) failed on error %d: %s", filename, ret, strerror(errno)); 
        free(DTX->connections[0]->dbh);
        free(DTX->connections[0]);
        free(DTX->connections);
        return EFAILURE;
      }
       
#ifdef DAEMON
      LOGDEBUG("initializing rwlock");
      pthread_rwlock_init(&DTX->connections[0]->rwlock, NULL);
#endif

    }
  }
#endif

  return 0;

#ifdef DAEMON
memerr:
  if (DTX) {
    if (DTX->connections) {
      if (DTX->connections[0]) 
        free(DTX->connections[0]->dbh);
      free(DTX->connections[0]);
    }
    free(DTX->connections);
  }
  LOG(LOG_CRIT, ERR_MEM_ALLOC);
  return EUNKNOWN;
#endif
  
}

int
dspam_shutdown_driver (DRIVER_CTX *DTX)
{

#ifdef DAEMON
  if (DTX && DTX->flags & DRF_STATEFUL) {
    hash_drv_map_t map;
    if (DTX->connections && DTX->connections[0]) {
      pthread_rwlock_destroy(&DTX->connections[0]->rwlock);
      map = (hash_drv_map_t) DTX->connections[0]->dbh;
      _hash_drv_close(map);
      free(DTX->connections[0]->dbh);
      free(DTX->connections[0]);
      free(DTX->connections);
    }
  }
#endif

  return 0;
}

int
_hash_drv_lock_get (
  DSPAM_CTX *CTX,
  struct _hash_drv_storage *s,
  const char *username)
{
  char filename[MAX_FILENAME_LENGTH];
  int r;

  _ds_userdir_path(filename, CTX->home, username, "lock");
  _ds_prepare_path_for(filename);

  s->lock = fopen(filename, "a");
  if (s->lock == NULL) {
    LOG(LOG_ERR, ERR_IO_FILE_OPEN, filename, strerror(errno));
    return EFAILURE;
  }
  r = _ds_get_fcntl_lock(fileno(s->lock));
  if (r) {
    fclose(s->lock);
    LOG(LOG_ERR, ERR_IO_LOCK, filename, r, strerror(errno));
  }
  return r;
}

int
_hash_drv_lock_free (struct _hash_drv_storage *s, const char *username)
{
  int r;

  if (username == NULL)
    return 0;

  r = _ds_free_fcntl_lock(fileno(s->lock));
  if (!r) {
    fclose(s->lock);
  } else {
    LOG(LOG_ERR, ERR_IO_LOCK_FREE, username, r, strerror(errno));
  }

  return r;
}

int _hash_drv_open(
  const char *filename, 
  hash_drv_map_t map, 
  unsigned long recmaxifnew) 
{
  struct _hash_drv_header header;
  int open_flags = O_RDWR;
  int mmap_flags = PROT_READ + PROT_WRITE;

  map->fd = open(filename, open_flags);
  if (map->fd < 0 && recmaxifnew) {
    FILE *f;
    struct _hash_drv_spam_record rec;
    long recno;

    memset(&header, 0, sizeof(struct _hash_drv_header));
    memset(&rec, 0, sizeof(struct _hash_drv_spam_record));

    header.hash_rec_max = recmaxifnew;

    f = fopen(filename, "w");
    if (!f) {
      LOG(LOG_ERR, ERR_IO_FILE_OPEN, filename, strerror(errno));
      return EFILE;
    }

    fwrite(&header, sizeof(struct _hash_drv_header), 1, f);

    for(recno=0;recno<header.hash_rec_max;recno++)
      fwrite(&rec, sizeof(struct _hash_drv_spam_record), 1, f);
    fclose(f);
    map->fd = open(filename, open_flags);
  }

  if (map->fd < 0) {
    LOG(LOG_ERR, ERR_IO_FILE_OPEN, filename, strerror(errno));
    return EFILE;
  }

  map->header = malloc(sizeof(struct _hash_drv_header));
  if (map->header == NULL) {
    LOG(LOG_CRIT, ERR_MEM_ALLOC);
    close(map->fd);
    map->addr = 0;
    return EFAILURE;
  }

  read(map->fd, map->header, sizeof(struct _hash_drv_header));
  map->file_len = sizeof(struct _hash_drv_header) + (map->header->hash_rec_max*sizeof(struct _hash_drv_spam_record));

  map->addr = mmap(NULL, map->file_len, mmap_flags, MAP_SHARED, map->fd, 0);
  if (map->addr == MAP_FAILED) {
    free(map->header);
    close(map->fd);
    map->addr = 0;
    return EFAILURE;
  }

  return 0;
}

int
_hash_drv_close(hash_drv_map_t map) {
  struct _hash_drv_header header;
  int r;

  if (!map->addr)
    return EINVAL;

  memcpy(&header, map->header, sizeof(struct _hash_drv_header));

  r = munmap(map->addr, map->file_len);
  if (r) {
    LOG(LOG_WARNING, "munmap failed on error %d: %s", r, strerror(errno));
  }
 
  /* Touch file */

  lseek (map->fd, 0, SEEK_SET);
  write (map->fd, &header, sizeof(struct _hash_drv_header));
  close(map->fd);

  map->addr = 0;
  free(map->header);

  return r;
}

int
_ds_init_storage (DSPAM_CTX * CTX, void *dbh)
{
  struct _hash_drv_storage *s = NULL;
  hash_drv_map_t map = NULL;
  int r1;

  if (CTX == NULL)
    return EINVAL;

  if (!CTX->home) {
    LOG(LOG_ERR, ERR_AGENT_DSPAM_HOME);
    return EINVAL;
  }

  if (CTX->flags & DSF_MERGED) {
    LOG(LOG_ERR, ERR_DRV_NO_MERGED);
    return EINVAL;
  }

  /* don't init if we're already initialized */
  if (CTX->storage != NULL)
    return EINVAL;

  s = calloc (1, sizeof (struct _hash_drv_storage));
  if (s == NULL)
  {
    LOG(LOG_CRIT, ERR_MEM_ALLOC);
    return EUNKNOWN;
  }

  if (dbh) {
    map = dbh;
    s->dbh_attached = 1;
  } else {
    map = calloc(1, sizeof(struct _hash_drv_map));
    if (!map) {
      LOG(LOG_CRIT, ERR_MEM_ALLOC);
      free(s);
      return EUNKNOWN;
    }
    s->dbh_attached = 0;
  }

  s->db = map;

  if (!dbh && CTX->username != NULL)
  {
    char db[MAX_FILENAME_LENGTH];
    int lock_result;

    if (CTX->group == NULL)
      _ds_userdir_path(db, CTX->home, CTX->username, "css");
    else
      _ds_userdir_path(db, CTX->home, CTX->group, "css");

    lock_result = _hash_drv_lock_get (CTX, s, 
      (CTX->group) ? CTX->group : CTX->username);
    if (lock_result < 0) 
      goto BAIL;

    if (_ds_read_attribute(CTX->config->attributes, "HashRecMax")) {
      r1 = _hash_drv_open(db, s->db, strtol(_ds_read_attribute(CTX->config->attributes, "HashRecMax"), NULL, 0));
    } else {
      r1 = _hash_drv_open(db, s->db, HASH_REC_MAX);
    }

    if (r1) {
      _hash_drv_close(s->db);
      free(s);
      return EFAILURE;
    }
  }

  CTX->storage = s;
  s->dir_handles = nt_create (NT_INDEX);

  if (_hash_drv_get_spamtotals (CTX))
  {
    LOGDEBUG ("unable to load totals.  using zero values.");
    memset (&CTX->totals, 0, sizeof (struct _ds_spam_totals));
  }

  return 0;

BAIL:
  free(s);
  return EFAILURE;
}

int
_ds_shutdown_storage (DSPAM_CTX * CTX)
{
  struct _hash_drv_storage *s;
  struct nt_node *node_nt;
  struct nt_c c_nt;
  int lock_result;

  if (!CTX || !CTX->storage)
    return EINVAL;

  s  = (struct _hash_drv_storage *) CTX->storage;

  node_nt = c_nt_first (s->dir_handles, &c_nt);
  while (node_nt != NULL)
  {
    DIR *dir;
    dir = (DIR *) node_nt->ptr;
    closedir (dir);
    node_nt = c_nt_next (s->dir_handles, &c_nt);
  }

  nt_destroy (s->dir_handles);

  if (CTX->operating_mode != DSM_CLASSIFY)
    _hash_drv_set_spamtotals (CTX);

  if (!s->dbh_attached) {
    _hash_drv_close(s->db);
    free(s->db);
    lock_result =
      _hash_drv_lock_free (s, (CTX->group) ? CTX->group : CTX->username);
    if (lock_result < 0)
      return EUNKNOWN;
  }

  free (CTX->storage);
  CTX->storage = NULL;

  return 0;
}

int
_hash_drv_get_spamtotals (DSPAM_CTX * CTX)
{
  struct _hash_drv_storage *s = (struct _hash_drv_storage *) CTX->storage;

  if (s->db->addr == 0)
    return EINVAL;
  memcpy(&CTX->totals, &s->db->header->totals, sizeof(struct _ds_spam_totals));
  return 0;
}

int
_hash_drv_set_spamtotals (DSPAM_CTX * CTX)
{
  struct _hash_drv_storage *s = (struct _hash_drv_storage *) CTX->storage;

  if (s->db->addr == NULL)
    return EINVAL;
  memcpy(&s->db->header->totals, &CTX->totals, sizeof (struct _ds_spam_totals));
  return 0;
}

int
_ds_getall_spamrecords (DSPAM_CTX * CTX, ds_diction_t diction)
{
  ds_term_t ds_term;
  ds_cursor_t ds_c;
  struct _ds_spam_stat stat;
  int ret = 0, x = 0;

  if (diction == NULL || CTX == NULL)
    return EINVAL;

  ds_c = ds_diction_cursor(diction);
  ds_term = ds_diction_next(ds_c);
  while(ds_term)
  {
    ds_term->s.spam_hits = 0;
    ds_term->s.innocent_hits = 0;
    x = _ds_get_spamrecord (CTX, ds_term->key, &stat);
    if (!x)
      ds_diction_setstat(diction, ds_term->key, &stat);
    else if (x != EFAILURE)
      ret = x;

    ds_term = ds_diction_next(ds_c);
  }
  ds_diction_close(ds_c);

  if (ret) {
    LOGDEBUG("_ds_getall_spamtotals returning %d", ret);
  }

  return ret;
}

int
_ds_setall_spamrecords (DSPAM_CTX * CTX, ds_diction_t diction)
{
  ds_term_t ds_term;
  ds_cursor_t ds_c;
  int ret = EUNKNOWN;

  if (diction == NULL || CTX == NULL)
    return EINVAL;

  if (CTX->operating_mode == DSM_CLASSIFY &&
        (CTX->training_mode != DST_TOE ||
          (diction->whitelist_token == 0 && (!(CTX->flags & DSF_NOISE)))))
  {
    return 0;
  }
                                                                                
  ds_c = ds_diction_cursor(diction);
  ds_term = ds_diction_next(ds_c);
  while(ds_term)
  {
    if (!(ds_term->s.status & TST_DIRTY)) {  
      ds_term = ds_diction_next(ds_c);
      continue;
    }

    if (CTX->training_mode == DST_TOE           &&
        CTX->classification == DSR_NONE         &&
        CTX->operating_mode == DSM_CLASSIFY     &&
        diction->whitelist_token != ds_term->key  &&
        (!ds_term->name || strncmp(ds_term->name, "bnr.", 4)))
    {
      ds_term = ds_diction_next(ds_c);
      continue;
    }

    if (ds_term->s.spam_hits > CTX->totals.spam_learned)
      ds_term->s.spam_hits = CTX->totals.spam_learned;
    if (ds_term->s.innocent_hits > CTX->totals.innocent_learned)
      ds_term->s.innocent_hits = CTX->totals.innocent_learned;

    if (!_ds_set_spamrecord (CTX, ds_term->key, &ds_term->s))
      ret = 0;
    ds_term = ds_diction_next(ds_c);
  }
  ds_diction_close(ds_c);

  return ret;
}

int
_ds_get_spamrecord (DSPAM_CTX * CTX, unsigned long long token,
                    struct _ds_spam_stat *stat)
{
  hash_drv_spam_record_t rec;
  struct _hash_drv_storage *s = (struct _hash_drv_storage *) CTX->storage;
  unsigned long thumb, filepos;
  int wrap;

  stat->probability = 0.00000;
  stat->status = 0;

  if (s->db->addr == NULL)
    return EINVAL;

  filepos = sizeof(struct _hash_drv_header) + ((token % s->db->header->hash_rec_max) * sizeof(struct _hash_drv_spam_record));
  thumb = filepos;
 
  wrap = 0;
  rec = s->db->addr+filepos;

  while(rec->hashcode != token && rec->hashcode != 0 && 
        ((wrap && filepos <thumb) || (!wrap)))
  {
    filepos += sizeof(struct _hash_drv_spam_record);

    if (!wrap && filepos >= (s->db->header->hash_rec_max * sizeof(struct _hash_drv_spam_record)))
    {
      filepos = sizeof(struct _hash_drv_header);
      wrap = 1;
    }
    rec = s->db->addr+filepos;
  }
  if (rec->hashcode == 0) 
    return EFAILURE;

  stat->innocent_hits = rec->nonspam;
  stat->spam_hits = rec->spam;
  return 0;
}

int
_ds_set_spamrecord (DSPAM_CTX * CTX, unsigned long long token,
                    struct _ds_spam_stat *stat)
{
  hash_drv_spam_record_t rec;
  struct _hash_drv_storage *s = (struct _hash_drv_storage *) CTX->storage;
  unsigned long filepos, thumb;
  int wrap, iterations;

  if (s->db->addr == NULL)
    return EINVAL;

  filepos = sizeof(struct _hash_drv_header) + ((token % s->db->header->hash_rec_max) * sizeof(struct _hash_drv_spam_record));
  thumb = filepos;

  wrap = 0;
  iterations = 0;
  rec = s->db->addr+filepos;
  while(rec->hashcode != token && rec->hashcode != 0 && 
        ((wrap && filepos<thumb) || (!wrap)))
  {
    iterations++;
    filepos += sizeof(struct _hash_drv_spam_record);

    if (!wrap && filepos >= (s->db->header->hash_rec_max * sizeof(struct _hash_drv_spam_record)))
    {
      filepos = sizeof(struct _hash_drv_header);
      wrap = 1;
    }
    rec = s->db->addr+filepos;
  }
  if (rec->hashcode != token && rec->hashcode != 0) {
    LOG(LOG_WARNING, "css table full. could not insert %llu. tried %lu times.", token, iterations);
    return EFAILURE;
  }
  rec->hashcode = token;
  rec->nonspam = stat->innocent_hits;
  rec->spam = stat->spam_hits;
  if (rec->nonspam < 0)
    rec->nonspam = 0;
  if (rec->spam < 0)
    rec->spam = 0;

  return 0;
}

int
_ds_set_signature (DSPAM_CTX * CTX, struct _ds_spam_signature *SIG,
                   const char *signature)
{
  char filename[MAX_FILENAME_LENGTH];
  char scratch[128];
  FILE *file;

  _ds_userdir_path(filename, CTX->home, (CTX->group) ? CTX->group : CTX->username, "sig");
  snprintf(scratch, sizeof(scratch), "/%s.sig", signature);
  strlcat(filename, scratch, sizeof(filename));
  _ds_prepare_path_for(filename);

  file = fopen(filename, "w");
  if (!file) {
    LOG(LOG_ERR, ERR_IO_FILE_WRITE, filename, strerror(errno));
    return EFAILURE;
  }
  fwrite(SIG->data, SIG->length, 1, file);
  fclose(file);

  return 0;
}

int
_ds_get_signature (DSPAM_CTX * CTX, struct _ds_spam_signature *SIG,
                   const char *signature)
{
  char filename[MAX_FILENAME_LENGTH];
  char scratch[128];
  FILE *file;
  struct stat statbuf;

  _ds_userdir_path(filename, CTX->home, (CTX->group) ? CTX->group : CTX->username, "sig");
  snprintf(scratch, sizeof(scratch), "/%s.sig", signature);
  strlcat(filename, scratch, sizeof(filename));

  if (stat (filename, &statbuf)) {
    LOG(LOG_ERR, ERR_IO_FILE_OPEN, filename, strerror(errno));
    return EFAILURE;
  };

  SIG->data = malloc(statbuf.st_size);
  if (!SIG->data) {
    LOG(LOG_CRIT, ERR_MEM_ALLOC);
    return EUNKNOWN;
  }

  file = fopen(filename, "r");
  if (!file) {
    LOG(LOG_ERR, ERR_IO_FILE_OPEN, filename, strerror(errno));
    return EFAILURE;
  }
  fread(SIG->data, statbuf.st_size, 1, file);
  SIG->length = statbuf.st_size;
  fclose(file);
  return 0;
}

void *_ds_connect (DSPAM_CTX *CTX)
{
  return NULL;
}

int
_ds_create_signature_id (DSPAM_CTX * CTX, char *buf, size_t len)
{
  char session[64];
  char digit[6];
  int pid, j;
 
  pid = getpid ();
  snprintf (session, sizeof (session), "%8lx%d", (long) time (NULL), pid);
 
  for (j = 0; j < 2; j++) 
  {  
    snprintf (digit, 6, "%d", rand ());
    strlcat (session, digit, 64);
  }

  strlcpy (buf, session, len);
  return 0;
}

int
_ds_verify_signature (DSPAM_CTX * CTX, const char *signature)
{
  char filename[MAX_FILENAME_LENGTH];
  char scratch[128];
  struct stat statbuf;

  _ds_userdir_path(filename, CTX->home, (CTX->group) ? CTX->group : CTX->username, "sig");
  snprintf(scratch, sizeof(scratch), "/%s.sig", signature);
  strlcat(filename, scratch, sizeof(filename));

  if (stat (filename, &statbuf)) 
    return 1;

  return 0;
}

struct _ds_storage_record *
_ds_get_nexttoken (DSPAM_CTX * CTX)
{
  struct _hash_drv_storage *s = (struct _hash_drv_storage *) CTX->storage;
  struct _hash_drv_spam_record rec;
  struct _ds_storage_record *sr;
  struct _ds_spam_stat stat;

  rec.hashcode = 0;

  sr = calloc(0, sizeof(struct _ds_storage_record));
  if (!sr) {
    LOG(LOG_CRIT, ERR_MEM_ALLOC);
    return NULL;
  }

  if (s->offset_nexttoken == 0)
    s->offset_nexttoken = sizeof(struct _hash_drv_header);

  while(rec.hashcode == 0) {
    s->offset_nexttoken += sizeof(struct _hash_drv_spam_record);
    if (s->offset_nexttoken > s->db->header->hash_rec_max * sizeof(struct _hash_drv_spam_record))
    { 
      free(sr);
      return NULL;
    }

    memcpy(&rec,s->db->addr+s->offset_nexttoken, sizeof(struct _hash_drv_spam_record));
    _ds_get_spamrecord (CTX, rec.hashcode, &stat);
  }

  sr->token = rec.hashcode;
  sr->spam_hits = stat.spam_hits;
  sr->innocent_hits = stat.innocent_hits;
  sr->last_hit = time(NULL);
  return sr;
}

int
_ds_delete_signature (DSPAM_CTX * CTX, const char *signature)
{
  char filename[MAX_FILENAME_LENGTH];
  char scratch[128];

  _ds_userdir_path(filename, CTX->home, (CTX->group) ? CTX->group : CTX->username, "sig");
  snprintf(scratch, sizeof(scratch), "/%s.sig", signature);
  strlcat(filename, scratch, sizeof(filename));  
  return unlink(filename);
}

/* TODO */

char * 
_ds_get_nextuser (DSPAM_CTX * CTX) 
{
  static char user[MAX_FILENAME_LENGTH];
  static char path[MAX_FILENAME_LENGTH];
  struct _hash_drv_storage *s = (struct _hash_drv_storage *) CTX->storage;
  struct nt_node *node_nt, *prev;
  struct nt_c c_nt;
  char *x = NULL, *y;
  DIR *dir = NULL;

  struct dirent *entry;

  if (s->dir_handles->items == 0)
  {
    char filename[MAX_FILENAME_LENGTH];
    snprintf(filename, MAX_FILENAME_LENGTH, "%s/data", CTX->home);
    dir = opendir (filename);
    if (dir == NULL)
    {
      LOG (LOG_WARNING,
           "unable to open directory '%s' for reading: %s",
           CTX->home, strerror (errno));
      return NULL;
    }

    nt_add (s->dir_handles, (void *) dir);
    strlcpy (path, filename, sizeof (path));
  }
  else
  {
    node_nt = c_nt_first (s->dir_handles, &c_nt);
    while (node_nt != NULL)
    {
      if (node_nt->next == NULL)
        dir = (DIR *) node_nt->ptr;
      node_nt = c_nt_next (s->dir_handles, &c_nt);
    }
  }

  while ((entry = readdir (dir)) != NULL)
  {
    struct stat st;
    char filename[MAX_FILENAME_LENGTH];
    snprintf (filename, sizeof (filename), "%s/%s", path, entry->d_name);

    if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
      continue;

    if (stat (filename, &st)) {
      continue;
    }

    /* push a new directory */
    if (st.st_mode & S_IFDIR)
    {
      DIR *ndir;

      ndir = opendir (filename);
      if (ndir == NULL)
        continue;
      strlcat (path, "/", sizeof (path));
      strlcat (path, entry->d_name, sizeof (path));
      nt_add (s->dir_handles, (void *) ndir);
      return _ds_get_nextuser (CTX);
    }
    else if (strlen(entry->d_name)>4 &&
      !strncmp ((entry->d_name + strlen (entry->d_name)) - 4, ".css", 4))
    {
      strlcpy (user, entry->d_name, sizeof (user));
      user[strlen (user) - 4] = 0;
      return user;
    }
  }

  /* pop current directory */
  y = strchr (path, '/');
  while (y != NULL)
  {
    x = y;
    y = strchr (x + 1, '/');
  }
  if (x)
    x[0] = 0;

  /* pop directory handle from list */
  node_nt = c_nt_first (s->dir_handles, &c_nt);
  prev = NULL;
  while (node_nt != NULL)
  {
    if (node_nt->next == NULL)
    {
      dir = (DIR *) node_nt->ptr;
      closedir (dir);
      if (prev != NULL) {
        prev->next = NULL;
        s->dir_handles->insert = NULL;
      }
      else
        s->dir_handles->first = NULL;
      free (node_nt);
      s->dir_handles->items--;
      prev = node_nt;
      break;
    }
    prev = node_nt;
    node_nt = c_nt_next (s->dir_handles, &c_nt);
  }
  if (s->dir_handles->items > 0)
    return _ds_get_nextuser (CTX);

  /* done */

  user[0] = 0;
  return NULL;
}

struct _ds_storage_signature *
_ds_get_nextsignature (DSPAM_CTX * CTX)
{
  return NULL;
}

int
_ds_delall_spamrecords (DSPAM_CTX * CTX, ds_diction_t diction)
{
  return 0;
}
