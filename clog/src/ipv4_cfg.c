/**
 * @file ipv4_cfg.c
 * @brief  
 * @author zhoujz, zhoujz@chinanetcenter.com
 * @version 1.0
 * @date 10/13/2014 15:57:25, 41th 星期一
 * @par Copyright:
 * Copyright (c) Chinanetcenter 2014. All rights reserved.
 */

#include "ipv4_cfg.h"
#include "ipv4_parse.h"
#include "str_replace.h"
#include "strsplit.h"

#define DEF_OLEN    (12)
#define CFG_FILE    "./config.txt"
#define IPATH_CFG    "path:input"
#define OPATH_CFG    "path:ouput"

#define IFMT_CFG    "log-format"
#define OFMT_CFG    "outlog-format"
#define AGRP_CFG    "auto-grp"
#define SGRP_CFG    "spc-grp"

#define KEY_OFFSET(key)  offsetof(ilog_t, key)
#define KEY_ILEN(key)    sizeof(((ilog_t*)0)->key)
#define ST_OFFSET(key)  offsetof(st_t, key)
#define ST_ILEN(key)    sizeof(((st_t*)0)->key)

/* key domain */
typedef struct _kmap_ {
    char          *kname;
    uint8_t       offset;
    uint8_t       ilen;
    uint8_t       olen;
    ipv4_parse_f  parse_func;
    uint8_t       st_off;
    uint8_t       st_len;
} kattr_map_t;

/* organize key attr in order of col to speed up parse */
typedef struct _ilog_kattr_ {
  int         num;
  kattr_map_t kattr[0];
} ilog_kattr_t;

/* domain cfg arry */
kattr_map_t key_attr_map[] = {
  {"srcip", KEY_OFFSET(srcip), KEY_ILEN(srcip), DEF_OLEN, ipv4_parse_ip},
  {"dstip", KEY_OFFSET(dstip), KEY_ILEN(dstip), DEF_OLEN, ipv4_parse_ip},
  {"sport", KEY_OFFSET(sport), KEY_ILEN(sport), DEF_OLEN, ipv4_parse_uint16},
  {"dport", KEY_OFFSET(dport), KEY_ILEN(dport), DEF_OLEN, ipv4_parse_uint16},
  {"proto_num", KEY_OFFSET(proto_num), KEY_ILEN(proto_num), DEF_OLEN, ipv4_parse_uint8},
  {"ifname", KEY_OFFSET(ifname), KEY_ILEN(ifname), DEF_OLEN, ipv4_parse_str},

  {"syn", KEY_OFFSET(syn), KEY_ILEN(syn), DEF_OLEN, ipv4_parse_uint32, 
    ST_OFFSET(syn), ST_ILEN(syn)},
  {"synack", KEY_OFFSET(synack), KEY_ILEN(synack), DEF_OLEN, ipv4_parse_uint32,
    ST_OFFSET(synack), ST_ILEN(synack)},
  {"rxpkts", KEY_OFFSET(rxpkts), KEY_ILEN(rxpkts), DEF_OLEN, ipv4_parse_uint32,
    ST_OFFSET(rxpkts), ST_ILEN(rxpkts)},
  {"txpkts", KEY_OFFSET(txpkts), KEY_ILEN(txpkts), DEF_OLEN, ipv4_parse_uint32,
    ST_OFFSET(txpkts), ST_ILEN(txpkts)},
  {"rxbytes", KEY_OFFSET(rxbytes), KEY_ILEN(rxbytes), DEF_OLEN, ipv4_parse_uint64,
    ST_OFFSET(rxbytes), ST_ILEN(rxbytes)},
  {"txbytes", KEY_OFFSET(txbytes), KEY_ILEN(txbytes), DEF_OLEN, ipv4_parse_uint64,
    ST_OFFSET(txbytes), ST_ILEN(txbytes)},
};

/* config list */
struct list_head cfg_list = LIST_HEAD_INIT(cfg_list);

/* format-cfg */
char        *infile_name;
char        *ofile_name;

dictionary  *ifmt_cfg = NULL;
dictionary  *ofmt_cfg = NULL;
dictionary  *agrp_cfg = NULL;
dictionary  *sgrp_cfg = NULL;

kattr_map_t *ipv4_cfg_kattr_get(const char *key);

/**
 * @brief Compute the hash key for a mem region
 *
 * @param mem [in] Memory Pointer
 * @param len [in] Memory length
 *
 * @return  hash 
 */
uint32_t ipv4_cfg_hash(char *mem, int len)
{
  uint32_t    hash ;
  size_t      i ;

  for (hash=0, i=0 ; i<len ; i++) {
    hash += (unsigned)mem[i] ;
    hash += (hash<<10);
    hash ^= (hash>>6) ;
  }

  hash += (hash <<3);
  hash ^= (hash >>11);
  hash += (hash <<15);

  hash %= HASH_SIZE;

  return hash ;
}

/**
 * @brief Malloc a new st_item
 *
 * @param kst [in] special the key stat instance that this stm belongs to
 *
 * @return  Pointer to stm -- success, NULL -- failure  
 */
st_item *ipv4_cfg_stm_malloc(key_st_t *kst)
{
  st_item   *stm;
  key_st_t  *nkst;

  stm = calloc(1, sizeof(st_item) + kst->ilen);
  //stm = malloc(sizeof(st_item) + kst->ilen);
  if (!stm) {
    return NULL;
  }

  INIT_HLIST_NODE(&stm->hn);
  INIT_LIST_HEAD(&stm->kst_list);
  stm->tm = time(NULL);
  stm->curkst = kst;
  if (kst->next) {
    nkst = ipv4_cfg_kst_ref(kst->next);
    list_add_tail(&nkst->list, &stm->kst_list);
  }

  return stm;
}

/**
 * @brief Free the Stm and its sub-kst
 *
 * @param stm [in] Pointer to stm 
 */
void ipv4_cfg_stm_free(st_item *stm)
{
  key_st_t *kst, *n;
  if (stm == NULL) {
    return;
  }

  /* remove itself from hlist */
  hlist_del(&stm->hn);

  /* del sub kst instance */
  if (!list_empty(&stm->kst_list)) {
    list_for_each_entry_safe(kst, n, &stm->kst_list, list) {
      ipv4_cfg_kst_release(kst);
    }
  }

  free(stm);

  return;
}

/**
 * @brief Dup & ref kst
 *
 * @param kst [in] kst 
 *
 * @return  Pointer -- success, NULL -- failure  
 *
 * Called when new instance generate
 */
key_st_t *ipv4_cfg_kst_ref(key_st_t *kst)
{
  key_st_t  *nkst;
  if (!kst) {
    return NULL;
  }

  nkst = malloc(sizeof(key_st_t));
  if (!nkst) {
    return NULL;
  }
  /* copy Pointer: next/cond/name, and other attr */
  memcpy(nkst, kst, sizeof(key_st_t) - sizeof(struct hlist_head) * HASH_SIZE);
  INIT_LIST_HEAD(&kst->list);
  memset(nkst->hlist, 0, sizeof(struct hlist_head) * HASH_SIZE);

  return nkst;
}

/**
 * @brief Release kst, include it's sub-kst and instance
 *
 * @param kst [in] Pointer of kst
 *
 * Called when instance del/free
 */
void ipv4_cfg_kst_release(key_st_t *kst)
{
  int         i;
  st_item     *st;

  struct hlist_node  *pos, *n;  

  if (kst == NULL) {
    return;
  }
  /* remote from list */
  list_del(&kst->list);
  /* delete stm */
  for (i = 0; i < HASH_SIZE; i++) {
    if (hlist_empty(&kst->hlist[i])) {
      continue;
    }
    hlist_for_each_entry_safe(st, pos, n, &kst->hlist[i], hn) {
      ipv4_cfg_stm_free(st);
    }
  }

  /* only free cur kst, don't free next/name/cond thing */
  free(kst);

  return;
}

/**
 * @brief Malloc a new kst instance
 *
 * @return  Pointer to kst -- success, NULL -- failure  
 *
 * Mostly called when generate new config
 */
key_st_t *ipv4_cfg_kst_malloc(void)
{
  int       i;
  key_st_t  *kst;
  
  kst = calloc(1, sizeof(key_st_t));
  if (!kst) {
    return kst;
  }
  INIT_LIST_HEAD(&kst->list);
  for (i = 0; i < HASH_SIZE; i++) {
    INIT_HLIST_HEAD(&kst->hlist[i]);
  }

  return kst;
}

/**
 * @brief Free kst, include it's sub-kst and instance
 *
 * @param kst [in] Pointer of kst
 *
 * This func called when config item delete
 */
void ipv4_cfg_kst_free(key_st_t *kst)
{
  int         i;
  st_item     *st;
  cond_t      *cpos, *cn;

  struct hlist_node  *pos, *n;  

  if (kst == NULL) {
    return;
  }
  
  /* remote from list */
  list_del(&kst->list);
  
  /* only free (release not) do this */
  if (kst->next) {
    /* this domain don't free in sub-kst */
    kst->next->cond = NULL;
    ipv4_cfg_kst_free(kst->next);
  }

  if (kst->name) {
    free(kst->name);
  }

  /* only free in top kst */
  if (kst->cond) {
    list_for_each_entry_safe(cpos, cn, &kst->cond->list, list) {
      ipv4_cfg_cond_free(cpos);
    }
    ipv4_cfg_cond_free(kst->cond);
  }

  /* delete stm */
  for (i = 0; i < HASH_SIZE; i++) {
    if (hlist_empty(&kst->hlist[i])) {
      continue;
    }
    hlist_for_each_entry_safe(st, pos, n, &kst->hlist[i], hn) {
      ipv4_cfg_stm_free(st);
    }
  }

  return;
}

int ipv4_cfg_kst_init(key_st_t *kst, const char *key)
{
  kattr_map_t *kattr;
  uint32_t    hash;
  int         i, size;

  if (kst == NULL || key == NULL) {
    return -EINVAL;
  }

  kattr = ipv4_cfg_kattr_get(key);
  if (!kattr) {
    return -EINVAL;
  }
  kst->offset = kattr->offset;
  kst->ilen = kattr->ilen;
  kst->olen = kattr->olen;
  kst->name = xstrdup(kattr->kname);
  if (!kst->name) {
    return -ENOMEM;
  }

  /* 
  if (val) {
    stm = ipv4_cfg_stm_malloc(kst);
    if (!smt) {
      return -ENOMEM;
    }
    kattr->parse_func(stm->data, kst->ilen, val);
    hash = ipv4_cfg_hash(stm->data, kst->ilen);
    hlist_add_head(&kst->hlist)
  }
  */

  return 0;
}

void ipv4_cfg_cond_free(cond_t *cond)
{
  if (!cond) {
    return;
  }

  if (--cond->ref) {
    return;
  }

  list_del(&cond->list);

  if (cond->name) {
    free(cond->name);
    cond->name = NULL;
  }

  free(cond);

  return;
}

cond_t *ipv4_cfg_cond_get(char *stkey, char *thrd)
{
  kattr_map_t *stattr;
  cond_t      *cond;

  if (stkey == NULL || thrd == NULL) {
    return NULL;
  }
  stattr = ipv4_cfg_kattr_get(stkey);
  if (!stattr) {
    return NULL;
  }

  cond = malloc(sizeof(cond_t));
  if (!cond) {
    return NULL;
  }
  cond->name = strdup(stkey);
  if (cond->name) {
    goto err;
  }
  INIT_LIST_HEAD(&cond->list);
  cond->offset = stattr->st_off;
  cond->len = stattr->st_len;
  cond->threshold = (uint64_t)strtoll(thrd, NULL, 0);
  cond->ref = 1;

  return cond;
err:
  if (cond) {
    ipv4_cfg_cond_free(cond);
  }

  return NULL;
}

acfg_item_t *ipv4_cfg_aitem_get(char *aname, char *cfgval)
{
  int   i;
  int   iv;
  int   ic;
  char  *key;
  char  **ptr;
  acfg_item_t *acfg_item = NULL;

  if (aname == NULL || cfgval == NULL) {
    return NULL;
  }

  key = str_replace(cfgval, " ", "");
  if (!key) {
    return NULL;
  }

  printf("======key====:%s\n", key);

  iv = occurrences("|", key) + 1;
  ptr = malloc(sizeof(char *) * iv);
  if (!ptr) {
    goto out;
  }
  strsplit(key, ptr, "|"); 
  if (iv < 2) {
    goto out;
  }
  printf("======iv====:%d\n", iv);

  acfg_item = malloc(sizeof(acfg_item_t));
  if (!acfg_item) {
    goto out;
  }

  acfg_item->nk = occurrences(",", ptr[0]) + 1;
  acfg_item->keys = malloc(sizeof(char *) * acfg_item->nk);
  strsplit(ptr[0], acfg_item->keys, ",");
  printf("======iv====:%d\n", iv);

  acfg_item->ns = occurrences(",", ptr[1]) + 1;
  acfg_item->stat= malloc(sizeof(char *) * acfg_item->ns);
  acfg_item->threshold = malloc(sizeof(char *) * acfg_item->ns);
  strsplit(ptr[1], acfg_item->stat, ",");
  strsplit(ptr[2], acfg_item->threshold, ",");
  acfg_item->name = strdup(aname);

  if (!acfg_item->name) {
    ipv4_cfg_aitem_free(acfg_item);
    acfg_item = NULL;
  }

out:
  if (key) {
    free(key);
  }
  if (ptr) {
    for (i = 0; i < iv; i++) {
      free(ptr[i]);
    }
    free(ptr);
  }

  return acfg_item;
}

void ipv4_cfg_aitem_free(acfg_item_t *acfg_item)
{
  int i; 

  if (!acfg_item) {
    return;
  }

  if (!acfg_item->name) {
    return;
  }
  free(acfg_item->name);
  acfg_item->name =NULL;
  for (i = 0; i < acfg_item->nk; i++) {
    free(acfg_item->keys[i]);
  }
  for (i = 0; i < acfg_item->ns; i++) {
    free(acfg_item->stat[i]);
    free(acfg_item->threshold[i]);
  }

  free(acfg_item);

  return;
}

key_st_t *ipv4_cfg_aitem_add(key_st_t *tkst, acfg_item_t *acfg_item)
{
  key_st_t    *kst;
  key_st_t    *okst = NULL;
  key_st_t    *atkst = NULL;
  cond_t      *cond, *pos, *n;
  cond_t      *tcond = NULL;
  cond_t      *atcond = NULL;

  int         j;

  if (!acfg_item) {
    return NULL;
  }

  if (tkst) {
    okst = tkst;
    while ( okst->kst_type == KST_SPEC && okst->next) {
      okst = okst->next;
    }
    tcond = tkst->cond;
  }

  /* Generate KST for each agp key */
  for (j = 0; j < acfg_item->nk; j++) {
    kst = ipv4_cfg_kst_malloc();
    if (!kst) {
      goto err;
    }
    if (atkst == NULL) {
      atkst = kst;
    }
    kst->kst_type = KST_AGEN;
    if (ipv4_cfg_kst_init(kst, acfg_item->keys[j])) {
      goto err;
    }
    if (okst) {
      if (j == 0 && okst->kst_type == KST_AGEN) {
        list_add_tail(&okst->list, &kst->list);
      } else {
        okst->next = kst;
      }
    }
    okst = kst;
  }
  /* Generate ST_THRD_INS */
  for (j = 0; j < acfg_item->ns; j++) {
    cond = ipv4_cfg_cond_get(acfg_item->stat[j], acfg_item->threshold[j]);
    if (!cond) {
      goto err;
    }
    if (atcond == NULL) {
      atcond = cond;
      if (!tcond) {
        tcond = atcond;
      } else {
        list_add_tail(&atcond->list, &tcond->list);
      }
    } else {
      list_add_tail(&cond->list, &tcond->list);
    }
  }

  /* sub-kst get the same cond with tskt */
  kst = tkst ? tkst->next : NULL;
  while (kst) {
    kst->cond = tcond;
    kst = kst->next;
  }

  return atkst;
err:
  if (atkst) {
    ipv4_cfg_kst_free(atkst);
  }
  if (atcond) {
    if (tcond != atcond) {
      pos = atcond;
      list_for_each_entry_safe_from(pos, n, &tcond->list, list) {
        ipv4_cfg_cond_free(pos);
      }
    } else {
      list_for_each_entry_safe(pos, n, &tcond->list, list) {
        ipv4_cfg_cond_free(pos);
      }
      ipv4_cfg_cond_free(tcond);
    }
  }

  return NULL;
}

/**
 * @brief Generate config item from config keys&value
 *
 * @param num [in] keys & vals number
 * @param keys [in] keys array
 * @param vals [in] vals array
 *
 * @return  key stat struct -- success, NULL -- failure  
 */
key_st_t *ipv4_cfg_item_init(int num, char **keys, char **vals, dictionary *dcfg)
{
  int         i, j;
  key_st_t    *tkst = NULL;
  key_st_t    *kst, *okst;
  dictionary  *ifmt_cfg;
  dictionary  *ofmt_cfg;
  acfg_item_t *acfg_item = NULL;

  /* get sub config, not need to free sub-dict */
  ifmt_cfg = iniparser_str_getsec(dcfg, IFMT_CFG);
  if (!ifmt_cfg) {
    return NULL;
  }
  
  ofmt_cfg = iniparser_str_getsec(dcfg, OFMT_CFG);
  if (!ofmt_cfg) {
    return NULL;
  }

  /* generate config kst-list */
  okst = NULL;
  for (i = 0; i < num; i++) {
    if (iniparser_find_entry(ifmt_cfg, keys[i])) {
      kst = ipv4_cfg_kst_malloc();
      if (!kst) {
        goto err;
      }
      if (!tkst) {
        tkst = kst;
      }

      if (okst) {
        okst->next = kst;
      }
      okst = kst;

      kst->kst_type = KST_SPEC;
      if (ipv4_cfg_kst_init(kst, keys[i])) {
        goto err;
      }
      continue;
    } else {
      acfg_item = ipv4_cfg_aitem_get(keys[i], vals[i]);
      kst = ipv4_cfg_aitem_add(tkst, acfg_item);
      if (!tkst) {
        tkst = kst;
      }
    }
  }

  /* kst-list is ready, add special stm */

err:
  if (kst) {
    ipv4_cfg_kst_free(tkst);
    kst = NULL;
  }

  return NULL;
}

/**
 * @brief Parse special auto generate group config 
 *
 * @param cfg [in] Config Item List
 * @param sdict [in] Sub-dictionary of sepcial agp 
 * @param ifmt_cfg [in] Sub-dictionary of log format config
 * @param ofmt_cfg [in] Sub-dictionary of output format config
 *
 * @return  0 -- success, other -- failure  
 */
int ipv4_cfg_sgpcfg_gen(struct list_head *cfg, dictionary *dcfg)
{
  int         i, j;
  int         snum;
  int         knum;
  char        **keys;
  char        **vals;
  dictionary  *sdict;
  dictionary  *d;
  cfg_t       *cfgitem;

  sdict = iniparser_str_getsec(dcfg, SGRP_CFG);
  if (!sdict) {
      return -EINVAL;
  }

  snum = iniparser_getnsec(sdict);

  for (i = 0; i < snum; i++) {
    cfgitem = malloc(sizeof(cfg_t));
    cfgitem->name = xstrdup(iniparser_getsecname(sdict, i));
    knum = iniparser_getsecnkeys(sdict, cfgitem->name);
    keys = iniparser_getseckeys(sdict, cfgitem->name);
    vals = iniparser_getsecvals(sdict, cfgitem->name);
    cfgitem->keyst = ipv4_cfg_item_init(knum, keys, vals, dcfg);
  }

  return 0;
}

kattr_map_t *ipv4_cfg_kattr_get(const char *key)
{
  int i, size;

  size = ARRAY_SIZE(key_attr_map);

  for (i = 0; i < size; i++) {
    if (!strcmp(key, key_attr_map[i].kname)) {
      return &key_attr_map[i];
    }
  }

  return NULL;
}

ilog_kattr_t *ipv4_cfg_kattr_init(dictionary *dcfg, kattr_map_t *map, int msize)
{
  ilog_kattr_t  *ilog_kattr = NULL;
  kattr_map_t   *kattr = NULL;
  int           i, isize, osize;
  char          **keys = NULL;
  int           col, space, max_col;

  if (dcfg == NULL || map == NULL) {
    return NULL;
  }

  /* get sub config, not need to free sub-dict */
  ifmt_cfg = iniparser_str_getsec(dcfg, IFMT_CFG);
  if (!ifmt_cfg) {
      goto out;
  }
  
  ofmt_cfg = iniparser_str_getsec(dcfg, OFMT_CFG);
  if (!ofmt_cfg) {
      goto out;
  }

  isize = iniparser_getsecnkeys(dcfg, IFMT_CFG);
  keys = iniparser_getseckeys(dcfg, IFMT_CFG);

  max_col = 0;
  for (i = 0; i < isize; i++) {
    col = iniparser_getint(ifmt_cfg, keys[i], -1);
    assert(col >= 0);
    if (max_col < col) {
      max_col = col;
    }
  }
  ilog_kattr = calloc(1, sizeof(ilog_kattr_t) + max_col * sizeof(kattr_map_t));
  ilog_kattr->num = max_col;
  for (i = 0; i < isize; i++) {
    col = iniparser_getint(ifmt_cfg, keys[i], -1);
    if (col < 0) {
      continue;
    }

    kattr = ipv4_cfg_kattr_get(keys[i]);
    if (kattr) {
      kattr->olen = iniparser_getint(ofmt_cfg, keys[i], DEF_OLEN);
      memcpy(&ilog_kattr->kattr[col], kattr, sizeof(kattr_map_t));
    }
  }

out:
  if (keys) {
    free(keys);
  }

  return ilog_kattr;
}

/**
 * @brief Load config file
 *
 * @param cfgname [in] Config file path name
 *
 * @return  0 -- success, other -- failure  
 */
int ipv4_readcfg(char *cfgname)
{
  int         rv = 0;
  dictionary  *dcfg;

  dcfg = iniparser_load(CFG_FILE);
  if (!dcfg) {
    rv = -ENOENT;
    goto out;
  }

  infile_name = iniparser_getstring(dcfg, IPATH_CFG, NULL);
  if (!infile_name) {
    rv = -EINVAL;
    goto out;
  }

  ofile_name = iniparser_getstring(dcfg, OPATH_CFG, NULL);
  if (!infile_name) {
    rv = -EINVAL;
    goto out;
  }

  agrp_cfg = iniparser_str_getsec(dcfg, AGRP_CFG);
  if (!agrp_cfg) {
      rv = -EINVAL;
      goto out;
  }

  
out:
  if (dcfg) {
    iniparser_freedict(dcfg);
  }

  return rv;
}

/**
 * @brief dump Key attr map
 */
void dump_key_attr_map(void) 
{
  int     i, size;
  ilog_t  *ilog = NULL;

  size = ARRAY_SIZE(key_attr_map);

  for (i = 0; i < size; i++) {
    fprintf(stdout, "key:%s, offset:%d, len:%d\n", 
      key_attr_map[i].kname,key_attr_map[i].offset, key_attr_map[i].ilen);
  }

  /* test str_replace */
  char    *buf;
  buf = str_replace("a aa aaa  ", " ", "");
  fprintf(stdout, "%s\n", buf);
  free(buf);

  char *cfgname = "acfg00";
  char *cfgkey = "srcip, dport | rxpkts | 1000";
  acfg_item_t *acfg_item;
  
  acfg_item = ipv4_cfg_aitem_get(cfgname, cfgkey);
  if (!acfg_item) {
    fprintf(stdout, "Get acfg_item fail!\n");
    return;
  }

  fprintf(stdout, "%s:\n", acfg_item->name);
  fprintf(stdout, "nk:%d, ns:%d\n", acfg_item->nk, acfg_item->ns);
  for (i = 0; i < acfg_item->nk; i++) {
    fprintf(stdout, "%s\n", acfg_item->keys[i]);
  }
  for (i = 0; i < acfg_item->ns; i++) {
    fprintf(stdout, "%s, %s\n", acfg_item->stat[i], acfg_item->threshold[i]);
  }

  ipv4_cfg_aitem_free(acfg_item);
  

  return;
}
