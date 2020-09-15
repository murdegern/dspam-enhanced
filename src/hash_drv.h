/* $Id: hash_drv.h,v 1.19 2011/06/28 00:13:48 sbajic Exp $ */

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

#ifndef _HASH_DRV_H
#  define _HASH_DRV_H

#ifdef HAVE_CONFIG_H
#include <auto-config.h>
#endif

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "nodetree.h"
#include "libdspam.h"

#define HASH_RECLEN_MAX	(60*1024*1024)

#define HASH_REC_MAX	98317
#define HASH_EXTENT_MAX 49157
#define HASH_SEEK_MAX   100

#define HASH_FILE_FLAG_HASHFN_MASK	(3u << 0)
#define HASH_FILE_FLAG_HASHFN_DIV	(0u << 0)
#define HASH_FILE_FLAG_HASHFN_POW2_0	(1u << 0)

struct _hash_drv_spam_record;
struct _hash_drv_header;
struct hash_drv_extent {
	unsigned long	hash_rec_max;
	size_t		offset;
	size_t		next_offset;
	unsigned int	idx;

	struct _hash_drv_header const	*header;
	struct _hash_drv_spam_record	*records;
	size_t				num_records;

	enum {
		HASH_DRV_HASH_DIV,
		HASH_DRV_HASH_POW2_0,
	}		hash_fn;
	unsigned long	hash_op;

	bool		is_broken;
};

struct _hash_drv_header
{
  unsigned long hash_rec_max;
  struct _ds_spam_totals totals;
  uint32_t flags;
} __attribute__((__aligned__(8)));
typedef struct _hash_drv_header *hash_drv_header_t;

typedef struct _hash_drv_map
{
  void *addr;
  int fd;
  size_t file_len;
  char filename[MAX_FILENAME_LENGTH];
  unsigned long max_seek;
  unsigned long max_extents;
  unsigned long extent_size;
  int pctincrease;
  int flags;

  struct hash_drv_extent	*extents;
  size_t			num_extents;
} *hash_drv_map_t;

typedef struct _hash_drv_storage
{
  hash_drv_map_t map;
  FILE *lock;
  int dbh_attached;

  unsigned long offset_nexttoken;
  hash_drv_header_t offset_header;
  unsigned long hash_rec_max;
  unsigned long max_seek;
  unsigned long max_extents;
  unsigned long extent_size;
  int pctincrease;
  int flags;

  struct nt *dir_handles;
} *hash_drv_storage_t;

typedef struct _hash_drv_spam_record
{
  unsigned long long hashcode;
  unsigned long nonspam;
  unsigned long spam;
} *hash_drv_spam_record_t;

int _hash_drv_get_spamtotals 
  (DSPAM_CTX * CTX);

int _hash_drv_set_spamtotals
  (DSPAM_CTX * CTX);

int _hash_drv_lock_get (
  DSPAM_CTX *CTX,
  struct _hash_drv_storage *s, 
  const char *username);

int _hash_drv_lock_free (
  struct _hash_drv_storage *s,
  const char *username);

/* lock variant used by css tools */
FILE* _hash_tools_lock_get (const char *cssfilename);

int _hash_tools_lock_free (
  const char *cssfilename,
  FILE* lockfile);

int _hash_drv_open(
  const char *filename,
  hash_drv_map_t map,
  unsigned long recmaxifnew,
  unsigned long max_seek,
  unsigned long max_extents,
  unsigned long extent_size,
  int pctincrease,
  int flags);

int _hash_drv_close
  (hash_drv_map_t map);

int
_hash_drv_set_spamrecord (
  hash_drv_map_t map,
  hash_drv_spam_record_t wrec,
  unsigned long map_offset);

struct hash_drv_extent *
_hash_drv_next_extent(hash_drv_map_t map, struct hash_drv_extent const *prev);

static inline bool
hash_drv_ext_is_eof(struct _hash_drv_map const *map,
		    struct hash_drv_extent const *ext)
{
	return ext->is_broken || ext->next_offset >= map->file_len;
}

void hash_drv_ext_prefetch(struct hash_drv_extent const *ext);

#define HSEEK_INSERT	0x01

#define HMAP_AUTOEXTEND	0x01
#define HMAP_HOLES	0x02
#define HMAP_FALLOCATE	0x04
#define HMAP_POW2	0x08
#define HMAP_ALLOW_BROKEN	0x10

#endif /* _HASH_DRV_H */
