/* $Id: csscompress.c,v 1.838 2011/11/16 00:19:14 sbajic Exp $ */

/*
 DSPAM
 COPYRIGHT (C) 2002-2012 DSPAM PROJECT

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/* csscompress.c - Compress a hash database's extents */

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
#include <libgen.h>
#include <errno.h>

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

#define READ_ATTRIB(A)          _ds_read_attribute(agent_config, A)
#define MATCH_ATTRIB(A, B)      _ds_match_attribute(agent_config, A, B)

int DO_DEBUG 
#ifdef DEBUG
= 1
#else
= 0
#endif
;

#include "read_config.h"
#include "hash_drv.h"
#include "error.h"
#include "language.h"
#include "utils.h"
 
#define SYNTAX "syntax: csscompress [filename]" 

int csscompress(const char *filename);

int main(int argc, char *argv[]) {
  int r;

  if (argc<2) {
    fprintf(stderr, "%s\n", SYNTAX);
    exit(EXIT_FAILURE);
  }
   
  agent_config = read_config(get_config_path(argc, argv));
  if (!agent_config) {
    LOG(LOG_ERR, ERR_AGENT_READ_CONFIG);
    exit(EXIT_FAILURE);
  }

  r = csscompress(argv[1]);
  
  if (r) {
    fprintf(stderr, "csscompress failed on error %d\n", r);
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}

int csscompress(const char *filename) {
  unsigned long i;
  struct hash_drv_extent const *ext;
  unsigned long reclen;
  struct _hash_drv_map old, new;
  hash_drv_spam_record_t rec;
  char newfile[128];
  struct stat st;
  char *filenamecopy;
  unsigned long max_seek     = HASH_SEEK_MAX;
  unsigned long max_extents  = 0;
  unsigned long extent_size  = HASH_EXTENT_MAX;
  int pctincrease = 0;
  int flags = 0;
  FILE* lockfile = NULL;
  int rc = EFAILURE;
  ssize_t opt_l;

  if (READ_ATTRIB("HashExtentSize"))
    extent_size = strtol(READ_ATTRIB("HashExtentSize"), NULL, 0);

  if (READ_ATTRIB("HashMaxExtents"))
    max_extents = strtol(READ_ATTRIB("HashMaxExtents"), NULL, 0);

  if (MATCH_ATTRIB("HashAutoExtend", "on"))
    flags |= HMAP_AUTOEXTEND;

  if (!MATCH_ATTRIB("HashNoHoles", "on"))
    flags |= HMAP_HOLES;

  if (MATCH_ATTRIB("HashPow2", "on"))
    flags |= HMAP_POW2;

  if (MATCH_ATTRIB("HashFallocate", "on"))
    flags |= HMAP_FALLOCATE;

  if (READ_ATTRIB("HashMaxSeek"))
     max_seek = strtol(READ_ATTRIB("HashMaxSeek"), NULL, 0);

  if (READ_ATTRIB("HashPctIncrease")) {
    pctincrease = atoi(READ_ATTRIB("HashPctIncrease"));
    if (pctincrease > 100) {
        LOG(LOG_ERR, "HashPctIncrease out of range; ignoring");
        pctincrease = 0;
    }
  }

  filenamecopy = strdup(filename);
  if (filenamecopy == NULL)
    return EFAILURE;

  lockfile = _hash_tools_lock_get (filename);

  snprintf(newfile, sizeof(newfile), "/%s/.dspam%u.css", dirname((char *)filenamecopy), (unsigned int) getpid());

  if (stat(filename, &st) < 0)
    goto end;

  if (_hash_drv_open(filename, &old, 0, max_seek,
                     max_extents, extent_size, pctincrease,
		     flags | HMAP_ALLOW_BROKEN))
    goto end;

  ext = NULL;
  reclen = 0;
  do {
	  ext = _hash_drv_next_extent(&old, ext);
	  if (!ext)
		  break;

	  hash_drv_ext_prefetch(ext);

	  for (i = 0; i < ext->num_records; ++i) {
		  rec = &ext->records[i];

		  if (rec->hashcode)
			  ++reclen;
	  }
  } while (!hash_drv_ext_is_eof(&old, ext));

  flags &= ~(HMAP_HOLES);
  flags |= HMAP_FALLOCATE;

  if (reclen < HASH_RECLEN_MAX / 4)
	  reclen *= 4;
  else
	  reclen = HASH_RECLEN_MAX;

  if (_hash_drv_open(newfile, &new, reclen,  max_seek,
                     max_extents, extent_size, pctincrease, flags))
  {
    _hash_drv_close(&old);
    goto end;
  }

  /* preserve counters, permissions, and ownership */
  {
	  struct _hash_drv_header const	*old_hdr = old.addr;
	  struct _hash_drv_header	*new_hdr = new.addr;
	  memcpy(&new_hdr->totals, &old_hdr->totals, sizeof new_hdr->totals);
  }

  if (fchown(new.fd, st.st_uid, st.st_gid) < 0) {
    _hash_drv_close(&new);
    _hash_drv_close(&old);
    unlink(newfile);
    goto end;
  }

  if (fchmod(new.fd, st.st_mode) < 0) {
    _hash_drv_close(&new);
    _hash_drv_close(&old);
    unlink(newfile);
    goto end;
  }

  ext = NULL;
  do {
	  ext = _hash_drv_next_extent(&old, ext);
	  if (!ext)
		  break;

	  printf("compressing %lu records in extent %u\n",
		 ext->num_records, ext->idx);

	  hash_drv_ext_prefetch(ext);

	  for (i = 0; i < ext->num_records; ++i) {
		  rec = &ext->records[i];

		  if (rec->hashcode) {
			  rc = _hash_drv_set_spamrecord(&new, rec, 0);
			  if (rc < 0) {
				  LOG(LOG_WARNING, "aborting on error");
				  _hash_drv_close(&new);
				  _hash_drv_close(&old);
				  unlink(newfile);
				  goto end;
			  }

			  if (rc) {
				  LOG(LOG_DEBUG,
				      "%s: extent #%u@%zu+%lu: hashcode %llx already used",
				      old.filename, ext->idx, ext->offset, i,
				      rec->hashcode);
			  }
		  }
	  }
  } while (!hash_drv_ext_is_eof(&old, ext));

  opt_l = css_optimize(&new, old.flags & ~HMAP_ALLOW_BROKEN);
  if (opt_l < 0) {
	  LOG(LOG_ERR, "%s: failed to optimize CSS", old.filename);
	  rc = EXIT_FAILURE;
	  _hash_drv_close(&new);
	  _hash_drv_close(&old);
	  unlink(newfile);
	  goto end;
  }

  if (opt_l > 0)
	  LOG(LOG_INFO, "%s: freed %zd bytes", old.filename, opt_l);

  _hash_drv_close(&new);
  _hash_drv_close(&old);
  if (rename(newfile, filename))
    fprintf(stderr, "rename(%s, %s): %s\n", newfile, filename, strerror(errno));

  rc = EXIT_SUCCESS;

end:
  _hash_tools_lock_free(filename, lockfile);

  return rc;
}
