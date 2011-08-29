/*************************************************************************************************
 * Implementation of Villa
 *                                                      Copyright (C) 2000-2005 Mikio Hirabayashi
 * This file is part of QDBM, Quick Database Manager.
 * QDBM is free software; you can redistribute it and/or modify it under the terms of the GNU
 * Lesser General Public License as published by the Free Software Foundation; either version
 * 2.1 of the License or any later version.  QDBM is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * You should have received a copy of the GNU Lesser General Public License along with QDBM; if
 * not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA.
 *************************************************************************************************/


#include "villa.h"
#include "myconf.h"

#define VL_LEAFIDMIN   1                 /* minimum number of leaf ID */
#define VL_NODEIDMIN   100000000         /* minimum number of node ID */
#define VL_VNUMBUFSIZ  8                 /* size of a buffer for variable length number */
#define VL_NUMBUFSIZ   32                /* size of a buffer for a number */
#define VL_LEVELMAX    64                /* max level of B+ tree */
#define VL_DEFLRECMAX  49                /* default number of records in each leaf */
#define VL_DEFNIDXMAX  192               /* default number of indexes in each node */
#define VL_DEFLCNUM    1024              /* default number of leaf cache */
#define VL_DEFNCNUM    512               /* default number of node cache */
#define VL_ALIGNRATIO  1.4               /* ratio between alignment and page size */
#define VL_CACHEOUT    8                 /* number of pages in a process of cacheout */
#define VL_INITBNUM    32749             /* initial bucket number */
#define VL_INITALIGN   448               /* initial size of alignment */
#define VL_OPTALIGN    -3                /* alignment setting when optimization */
#define VL_PATHBUFSIZ  1024              /* size of a path buffer */
#define VL_TMPFSUF     MYEXTSTR "vltmp"  /* suffix of a temporary file */
#define VL_ROOTKEY     -1                /* key of the root key */
#define VL_LASTKEY     -2                /* key of the last key */
#define VL_LNUMKEY     -3                /* key of the number of leaves */
#define VL_NNUMKEY     -4                /* key of the number of nodes */
#define VL_RNUMKEY     -5                /* key of the number of records */

enum {                                   /* enumeration for flags */
  VL_FLISVILLA = 1 << 0,                 /* whether for Villa or not */
  VL_FLISZLIB = 1 << 1                   /* whether with ZLIB or not */
};


/* private function prototypes */
static int vllexcompare(const char *aptr, int asiz, const char *bptr, int bsiz);
static int vlintcompare(const char *aptr, int asiz, const char *bptr, int bsiz);
static int vlnumcompare(const char *aptr, int asiz, const char *bptr, int bsiz);
static int vldeccompare(const char *aptr, int asiz, const char *bptr, int bsiz);
static int vldpputnum(DEPOT *depot, int knum, int vnum);
static int vldpgetnum(DEPOT *depot, int knum, int *vnp);
static int vlsetvnumbuf(char *buf, int num);
static int vlreadvnumbuf(const char *buf, int size, int *sp);
static VLLEAF *vlleafnew(VILLA *villa, int prev, int next);
static int vlleafcacheout(VILLA *villa, int id);
static int vlleafsave(VILLA *villa, VLLEAF *leaf);
static VLLEAF *vlleafload(VILLA *villa, int id);
static int vlleafaddrec(VILLA *villa, VLLEAF *leaf, int dmode,
                        const char *kbuf, int ksiz, const char *vbuf, int vsiz);
static VLLEAF *vlleafdivide(VILLA *villa, VLLEAF *leaf);
static VLNODE *vlnodenew(VILLA *villa, int heir);
static int vlnodecacheout(VILLA *villa, int id);
static int vlnodesave(VILLA *villa, VLNODE *node);
static VLNODE *vlnodeload(VILLA *villa, int id);
static void vlnodeaddidx(VILLA *villa, VLNODE *node, int order,
                         int pid, const char *kbuf, int ksiz);
static int vlsearchleaf(VILLA *villa, const char *kbuf, int ksiz, int *hist, int *hnp);
static int vlcacheadjust(VILLA *villa);
static VLREC *vlrecsearch(VILLA *villa, VLLEAF *leaf, const char *kbuf, int ksiz, int *ip);



/*************************************************************************************************
 * public objects
 *************************************************************************************************/


/* Comparing functions. */
VLCFUNC VL_CMPLEX = vllexcompare;
VLCFUNC VL_CMPINT = vlintcompare;
VLCFUNC VL_CMPNUM = vlnumcompare;
VLCFUNC VL_CMPDEC = vldeccompare;


/* Get a database handle. */
VILLA *vlopen(const char *name, int omode, VLCFUNC cmp){
  DEPOT *depot;
  int dpomode, flags, zmode, root, last, lnum, nnum, rnum;
  VILLA *villa;
  VLLEAF *leaf;
  assert(name && cmp);
  dpomode = DP_OREADER;
  if(omode & VL_OWRITER){
    dpomode = DP_OWRITER;
    if(omode & VL_OCREAT) dpomode |= DP_OCREAT;
    if(omode & VL_OTRUNC) dpomode |= DP_OTRUNC;
  }
  if(omode & VL_ONOLCK) dpomode |= DP_ONOLCK;
  if(omode & VL_OLCKNB) dpomode |= DP_OLCKNB;
  if(!(depot = dpopen(name, dpomode, VL_INITBNUM))) return NULL;
  flags = dpgetflags(depot);
  zmode = 0;
  root = -1;
  last = -1;
  lnum = 0;
  nnum = 0;
  rnum = 0;
  if(dprnum(depot) > 0){
    if(!(flags & VL_FLISVILLA) ||
       !vldpgetnum(depot, VL_ROOTKEY, &root) || !vldpgetnum(depot, VL_LASTKEY, &last) ||
       !vldpgetnum(depot, VL_LNUMKEY, &lnum) || !vldpgetnum(depot, VL_NNUMKEY, &nnum) ||
       !vldpgetnum(depot, VL_RNUMKEY, &rnum) || root < VL_LEAFIDMIN || last < VL_LEAFIDMIN ||
       lnum < 0 || nnum < 0 || rnum < 0){
      dpclose(depot);
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return NULL;
    }
    zmode = flags & VL_FLISZLIB;
  } else if(omode & VL_OWRITER){
    zmode = omode & VL_OZCOMP;
  }
  if(omode & VL_OWRITER){
    flags |= VL_FLISVILLA;
    if(_qdbm_deflate && zmode) flags |= VL_FLISZLIB;
    if(!dpsetflags(depot, flags)){
      dpclose(depot);
      return NULL;
    }
  }
  CB_MALLOC(villa, sizeof(VILLA));
  villa->depot = depot;
  villa->cmp = cmp;
  villa->wmode = (omode & VL_OWRITER);
  villa->zmode = zmode;
  villa->root = root;
  villa->last = last;
  villa->lnum = lnum;
  villa->nnum = nnum;
  villa->rnum = rnum;
  villa->leafc = cbmapopen();
  villa->nodec = cbmapopen();
  villa->curleaf = -1;
  villa->curknum = -1;
  villa->curvnum = -1;
  villa->leafrecmax = VL_DEFLRECMAX;
  villa->nodeidxmax = VL_DEFNIDXMAX;
  villa->leafcnum = VL_DEFLCNUM;
  villa->nodecnum = VL_DEFNCNUM;
  villa->avglsiz = VL_INITALIGN;
  villa->avgnsiz = VL_INITALIGN;
  villa->tran = FALSE;
  villa->rbroot = -1;
  villa->rblast = -1;
  villa->rblnum = -1;
  villa->rbnnum = -1;
  villa->rbrnum = -1;
  if(root == -1){
    leaf = vlleafnew(villa, -1, -1);
    villa->root = leaf->id;
    villa->last = leaf->id;
    if(!vltranbegin(villa) || !vltranabort(villa)){
      vlclose(villa);
      return NULL;
    }
  }
  return villa;
}


/* Close a database handle. */
int vlclose(VILLA *villa){
  int err, pid;
  const char *tmp;
  assert(villa);
  err = FALSE;
  if(villa->tran){
    if(!vltranabort(villa)) err = TRUE;
  }
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlleafcacheout(villa, pid)) err = TRUE;
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlnodecacheout(villa, pid)) err = TRUE;
  }
  if(villa->wmode){
    if(!dpsetalign(villa->depot, 0)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_ROOTKEY, villa->root)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_LASTKEY, villa->last)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_LNUMKEY, villa->lnum)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_NNUMKEY, villa->nnum)) err = TRUE;
    if(!vldpputnum(villa->depot, VL_RNUMKEY, villa->rnum)) err = TRUE;
  }
  cbmapclose(villa->leafc);
  cbmapclose(villa->nodec);
  if(!dpclose(villa->depot)) err = TRUE;
  free(villa);
  return err ? FALSE : TRUE;
}


/* Store a record. */
int vlput(VILLA *villa, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int dmode){
  VLLEAF *leaf, *newleaf;
  VLNODE *node, *newnode;
  VLIDX *idxp;
  CBDATUM *key;
  int hist[VL_LEVELMAX];
  int i, hnum, pid, heir, parent, mid;
  assert(villa && kbuf && vbuf);
  villa->curleaf = -1;
  villa->curknum = -1;
  villa->curvnum = -1;
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(vsiz < 0) vsiz = strlen(vbuf);
  if((pid = vlsearchleaf(villa, kbuf, ksiz, hist, &hnum)) == -1) return FALSE;
  if(!(leaf = vlleafload(villa, pid))) return FALSE;
  if(!vlleafaddrec(villa, leaf, dmode, kbuf, ksiz, vbuf, vsiz)){
    dpecodeset(DP_EKEEP, __FILE__, __LINE__);
    return FALSE;
  }
  if(CB_LISTNUM(leaf->recs) > villa->leafrecmax && CB_LISTNUM(leaf->recs) % 2 == 0){
    if(!(newleaf = vlleafdivide(villa, leaf))) return FALSE;
    if(leaf->id == villa->last) villa->last = newleaf->id;
    heir = leaf->id;
    pid = newleaf->id;
    key = ((VLREC *)CB_LISTVAL(newleaf->recs, 0, NULL))->key;
    key = cbdatumopen(CB_DATUMPTR(key), CB_DATUMSIZE(key));
    while(TRUE){
      if(hnum < 1){
        node = vlnodenew(villa, heir);
        vlnodeaddidx(villa, node, TRUE, pid, CB_DATUMPTR(key), CB_DATUMSIZE(key));
        villa->root = node->id;
        cbdatumclose(key);
        break;
      }
      parent = hist[--hnum];
      if(!(node = vlnodeload(villa, parent))){
        cbdatumclose(key);
        return FALSE;
      }
      vlnodeaddidx(villa, node, FALSE, pid, CB_DATUMPTR(key), CB_DATUMSIZE(key));
      cbdatumclose(key);
      if(CB_LISTNUM(node->idxs) <= villa->nodeidxmax || CB_LISTNUM(node->idxs) % 2 == 0) break;
      mid = CB_LISTNUM(node->idxs) / 2;
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, mid, NULL);
      newnode = vlnodenew(villa, idxp->pid);
      heir = node->id;
      pid = newnode->id;
      key = cbdatumopen(CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key));
      for(i = mid + 1; i < CB_LISTNUM(node->idxs); i++){
        idxp = (VLIDX *)CB_LISTVAL(node->idxs, i, NULL);
        vlnodeaddidx(villa, newnode, TRUE, idxp->pid,
                     CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key));
      }
      for(i = 0; i <= mid; i++){
        idxp = (VLIDX *)cblistpop(node->idxs, NULL);
        cbdatumclose(idxp->key);
        free(idxp);
      }
      node->dirty = TRUE;
    }
  }
  if(!villa->tran && !vlcacheadjust(villa)) return FALSE;
  return TRUE;
}


/* Delete a record. */
int vlout(VILLA *villa, const char *kbuf, int ksiz){
  VLLEAF *leaf;
  VLREC *recp;
  int pid, ri, vsiz;
  char *vbuf;
  assert(villa && kbuf);
  villa->curleaf = -1;
  villa->curknum = -1;
  villa->curvnum = -1;
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((pid = vlsearchleaf(villa, kbuf, ksiz, NULL, NULL)) == -1) return FALSE;
  if(!(leaf = vlleafload(villa, pid))) return FALSE;
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, &ri))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(recp->rest){
    cbdatumclose(recp->first);
    vbuf = cblistshift(recp->rest, &vsiz);
    recp->first = cbdatumopen(vbuf, vsiz);
    free(vbuf);
    if(CB_LISTNUM(recp->rest) < 1){
      cblistclose(recp->rest);
      recp->rest = NULL;
    }
  } else {
    cbdatumclose(recp->key);
    cbdatumclose(recp->first);
    free(cblistremove(leaf->recs, ri, NULL));
  }
  leaf->dirty = TRUE;
  villa->rnum--;
  if(!villa->tran && !vlcacheadjust(villa)) return FALSE;
  return TRUE;
}


/* Retrieve a record. */
char *vlget(VILLA *villa, const char *kbuf, int ksiz, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  int pid;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((pid = vlsearchleaf(villa, kbuf, ksiz, NULL, NULL)) == -1) return NULL;
  if(!(leaf = vlleafload(villa, pid))) return NULL;
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  if(!villa->tran && !vlcacheadjust(villa)) return NULL;
  if(sp) *sp = CB_DATUMSIZE(recp->first);
  return cbmemdup(CB_DATUMPTR(recp->first), CB_DATUMSIZE(recp->first));
}


/* Get the number of records corresponding a key. */
int vlvnum(VILLA *villa, const char *kbuf, int ksiz){
  VLLEAF *leaf;
  VLREC *recp;
  int pid;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((pid = vlsearchleaf(villa, kbuf, ksiz, NULL, NULL)) == -1) return 0;
  if(!(leaf = vlleafload(villa, pid))) return 0;
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return 0;
  }
  if(!villa->tran && !vlcacheadjust(villa)) return 0;
  return 1 + (recp->rest ? CB_LISTNUM(recp->rest) : 0);
}


/* Store plural records corresponding a key. */
int vlputlist(VILLA *villa, const char *kbuf, int ksiz, const CBLIST *vals){
  int i, vsiz;
  const char *vbuf;
  assert(villa && kbuf && vals);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(CB_LISTNUM(vals) < 1){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  for(i = 0; i < CB_LISTNUM(vals); i++){
    vbuf = CB_LISTVAL2(vals, i, &vsiz);
    if(!vlput(villa, kbuf, ksiz, vbuf, vsiz, VL_DDUP)) return FALSE;
  }
  return TRUE;
}


/* Delete all records corresponding a key. */
int vloutlist(VILLA *villa, const char *kbuf, int ksiz){
  int i, vnum;
  assert(villa && kbuf);
  if(!villa->wmode){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((vnum = vlvnum(villa, kbuf, ksiz)) < 1) return FALSE;
  for(i = 0; i < vnum; i++){
    if(!vlout(villa, kbuf, ksiz)) return FALSE;
  }
  return TRUE;
}


/* Retrieve values of all records corresponding a key. */
CBLIST *vlgetlist(VILLA *villa, const char *kbuf, int ksiz){
  VLLEAF *leaf;
  VLREC *recp;
  int pid, i, vsiz;
  CBLIST *vals;
  const char *vbuf;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((pid = vlsearchleaf(villa, kbuf, ksiz, NULL, NULL)) == -1) return NULL;
  if(!(leaf = vlleafload(villa, pid))) return NULL;
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  vals = cblistopen();
  cblistpush(vals, CB_DATUMPTR(recp->first), CB_DATUMSIZE(recp->first));
  if(recp->rest){
    for(i = 0; i < CB_LISTNUM(recp->rest); i++){
      vbuf = CB_LISTVAL2(recp->rest, i, &vsiz);
      cblistpush(vals, vbuf, vsiz);
    }
  }
  if(!villa->tran && !vlcacheadjust(villa)){
    cblistclose(vals);
    return NULL;
  }
  return vals;
}


/* Retrieve concatenated values of all records corresponding a key. */
char *vlgetcat(VILLA *villa, const char *kbuf, int ksiz, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  int pid, i, vsiz, rsiz;
  char *rbuf;
  const char *vbuf;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((pid = vlsearchleaf(villa, kbuf, ksiz, NULL, NULL)) == -1) return NULL;
  if(!(leaf = vlleafload(villa, pid))) return NULL;
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, NULL))){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return NULL;
  }
  rsiz = CB_DATUMSIZE(recp->first);
  CB_MALLOC(rbuf, rsiz + 1);
  memcpy(rbuf, CB_DATUMPTR(recp->first), rsiz);
  if(recp->rest){
    for(i = 0; i < CB_LISTNUM(recp->rest); i++){
      vbuf = CB_LISTVAL2(recp->rest, i, &vsiz);
      CB_REALLOC(rbuf, rsiz + vsiz + 1);
      memcpy(rbuf + rsiz, vbuf, vsiz);
      rsiz += vsiz;
    }
  }
  rbuf[rsiz] = '\0';
  if(!villa->tran && !vlcacheadjust(villa)){
    free(rbuf);
    return NULL;
  }
  if(sp) *sp = rsiz;
  return rbuf;
}


/* Move the cursor to the first record. */
int vlcurfirst(VILLA *villa){
  VLLEAF *leaf;
  assert(villa);
  villa->curleaf = VL_LEAFIDMIN;
  villa->curknum = 0;
  villa->curvnum = 0;
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  while(CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = leaf->next;
    villa->curknum = 0;
    villa->curvnum = 0;
    if(villa->curleaf == -1){
      dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
      return FALSE;
    }
    if(!(leaf = vlleafload(villa, villa->curleaf))){
      villa->curleaf = -1;
      return FALSE;
    }
  }
  return TRUE;
}


/* Move the cursor to the last record. */
int vlcurlast(VILLA *villa){
  VLLEAF *leaf;
  VLREC *recp;
  assert(villa);
  villa->curleaf = villa->last;
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  while(CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = leaf->prev;
    if(villa->curleaf == -1){
      villa->curleaf = -1;
      dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
      return FALSE;
    }
    if(!(leaf = vlleafload(villa, villa->curleaf))){
      villa->curleaf = -1;
      return FALSE;
    }
  }
  villa->curknum = CB_LISTNUM(leaf->recs) - 1;
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum, NULL);
  villa->curvnum = recp->rest ? CB_LISTNUM(recp->rest) : 0;
  return TRUE;
}


/* Move the cursor to the previous record. */
int vlcurprev(VILLA *villa){
  VLLEAF *leaf;
  VLREC *recp;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf)) || CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum, NULL);
  villa->curvnum--;
  if(villa->curvnum < 0){
    villa->curknum--;
    if(villa->curknum < 0){
      villa->curleaf = leaf->prev;
      if(villa->curleaf == -1){
        villa->curleaf = -1;
        dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
        return FALSE;
      }
      if(!(leaf = vlleafload(villa, villa->curleaf))){
        villa->curleaf = -1;
        return FALSE;
      }
      while(CB_LISTNUM(leaf->recs) < 1){
        villa->curleaf = leaf->prev;
        if(villa->curleaf == -1){
          dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
          return FALSE;
        }
        if(!(leaf = vlleafload(villa, villa->curleaf))){
          villa->curleaf = -1;
          return FALSE;
        }
      }
      villa->curknum = CB_LISTNUM(leaf->recs) - 1;
      recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum, NULL);
      villa->curvnum = recp->rest ? CB_LISTNUM(recp->rest) : 0;
    }
    recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum, NULL);
    villa->curvnum = recp->rest ? CB_LISTNUM(recp->rest) : 0;
  }
  if(!villa->tran && !vlcacheadjust(villa)) return FALSE;
  return TRUE;
}


/* Move the cursor to the next record. */
int vlcurnext(VILLA *villa){
  VLLEAF *leaf;
  VLREC *recp;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf)) || CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum, NULL);
  villa->curvnum++;
  if(villa->curvnum > (recp->rest ? CB_LISTNUM(recp->rest) : 0)){
    villa->curknum++;
    villa->curvnum = 0;
  }
  if(villa->curknum >= CB_LISTNUM(leaf->recs)){
    villa->curleaf = leaf->next;
    villa->curknum = 0;
    villa->curvnum = 0;
    if(villa->curleaf == -1){
      dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
      return FALSE;
    }
    if(!(leaf = vlleafload(villa, villa->curleaf))){
      villa->curleaf = -1;
      return FALSE;
    }
    while(CB_LISTNUM(leaf->recs) < 1){
      villa->curleaf = leaf->next;
      villa->curknum = 0;
      villa->curvnum = 0;
      if(villa->curleaf == -1){
        dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
        return FALSE;
      }
      if(!(leaf = vlleafload(villa, villa->curleaf))){
        villa->curleaf = -1;
        return FALSE;
      }
    }
  }
  if(!villa->tran && !vlcacheadjust(villa)) return FALSE;
  return TRUE;
}


/* Move the cursor to position around a record. */
int vlcurjump(VILLA *villa, const char *kbuf, int ksiz, int jmode){
  VLLEAF *leaf;
  VLREC *recp;
  int pid, index;
  assert(villa && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if((pid = vlsearchleaf(villa, kbuf, ksiz, NULL, NULL)) == -1){
    villa->curleaf = -1;
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, pid))){
    villa->curleaf = -1;
    return FALSE;
  }
  while(CB_LISTNUM(leaf->recs) < 1){
    villa->curleaf = (jmode == VL_JFORWARD) ? leaf->next : leaf->prev;
    if(villa->curleaf == -1){
      dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
      return FALSE;
    }
    if(!(leaf = vlleafload(villa, villa->curleaf))){
      villa->curleaf = -1;
      return FALSE;
    }
  }
  if(!(recp = vlrecsearch(villa, leaf, kbuf, ksiz, &index))){
    if(jmode == VL_JFORWARD){
      villa->curleaf = leaf->id;
      if(index >= CB_LISTNUM(leaf->recs)) index--;
      villa->curknum = index;
      villa->curvnum = 0;
      recp = (VLREC *)CB_LISTVAL(leaf->recs, index, NULL);
      if(villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key)) < 0) return TRUE;
      villa->curvnum = (recp->rest ? CB_LISTNUM(recp->rest) : 0);
      return vlcurnext(villa);
    } else {
      villa->curleaf = leaf->id;
      if(index >= CB_LISTNUM(leaf->recs)) index--;
      villa->curknum = index;
      recp = (VLREC *)CB_LISTVAL(leaf->recs, index, NULL);
      villa->curvnum = (recp->rest ? CB_LISTNUM(recp->rest) : 0);
      if(villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key)) > 0) return TRUE;
      villa->curvnum = 0;
      return vlcurprev(villa);
    }
  }
  if(jmode == VL_JFORWARD){
    villa->curleaf = pid;
    villa->curknum = index;
    villa->curvnum = 0;
  } else {
    villa->curleaf = pid;
    villa->curknum = index;
    villa->curvnum = (recp->rest ? CB_LISTNUM(recp->rest) : 0);
  }
  return TRUE;
}


/* Get the key of the record where the cursor is. */
char *vlcurkey(VILLA *villa, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  const char *kbuf;
  int ksiz;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum, NULL);
  kbuf = CB_DATUMPTR(recp->key);
  ksiz = CB_DATUMSIZE(recp->key);
  if(sp) *sp = ksiz;
  return cbmemdup(kbuf, ksiz);
}


/* Get the value of the record where the cursor is. */
char *vlcurval(VILLA *villa, int *sp){
  VLLEAF *leaf;
  VLREC *recp;
  const char *kbuf;
  int ksiz;
  assert(villa);
  if(villa->curleaf == -1){
    dpecodeset(DP_ENOITEM, __FILE__, __LINE__);
    return FALSE;
  }
  if(!(leaf = vlleafload(villa, villa->curleaf))){
    villa->curleaf = -1;
    return FALSE;
  }
  recp = (VLREC *)CB_LISTVAL(leaf->recs, villa->curknum, NULL);
  if(villa->curvnum < 1){
    kbuf = CB_DATUMPTR(recp->first);
    ksiz = CB_DATUMSIZE(recp->first);
  } else {
    kbuf = CB_LISTVAL2(recp->rest, villa->curvnum - 1, &ksiz);
  }
  if(sp) *sp = ksiz;
  return cbmemdup(kbuf, ksiz);
}


/* Set the tuning parameters for performance. */
void vlsettuning(VILLA *villa, int lrecmax, int nidxmax, int lcnum, int ncnum){
  assert(villa);
  if(lrecmax < 1) lrecmax = VL_DEFLRECMAX;
  if(lrecmax < 3) lrecmax = 3;
  if(nidxmax < 1) nidxmax = VL_DEFNIDXMAX;
  if(nidxmax < 4) nidxmax = 4;
  if(lcnum < 1) lcnum = VL_DEFLCNUM;
  if(lcnum < VL_CACHEOUT * 2) lcnum = VL_CACHEOUT * 2;
  if(ncnum < 1) ncnum = VL_DEFNCNUM;
  if(ncnum < VL_CACHEOUT * 2) ncnum = VL_CACHEOUT * 2;
  villa->leafrecmax = lrecmax;
  villa->nodeidxmax = nidxmax;
  villa->leafcnum = lcnum;
  villa->nodecnum = ncnum;
}


/* Synchronize updating contents with the file and the device. */
int vlsync(VILLA *villa){
  int err, pid;
  const char *tmp;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlleafcacheout(villa, pid)) err = TRUE;
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!vlnodecacheout(villa, pid)) err = TRUE;
  }
  if(!dpsetalign(villa->depot, 0)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_ROOTKEY, villa->root)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LASTKEY, villa->last)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LNUMKEY, villa->lnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_NNUMKEY, villa->nnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_RNUMKEY, villa->rnum)) err = TRUE;
  if(!dpsync(villa->depot)) err = TRUE;
  return err ? FALSE : TRUE;
}


/* Optimize a database. */
int vloptimize(VILLA *villa){
  int err;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  if(!vlsync(villa)) return FALSE;
  if(!dpsetalign(villa->depot, VL_OPTALIGN)) err = TRUE;
  if(!dpoptimize(villa->depot, -1)) err = TRUE;
  return err ? FALSE : TRUE;
}


/* Get the name of a database. */
char *vlname(VILLA *villa){
  assert(villa);
  return dpname(villa->depot);
}


/* Get the size of a database file. */
int vlfsiz(VILLA *villa){
  return dpfsiz(villa->depot);
}


/* Get the number of the leaf nodes of B+ tree. */
int vllnum(VILLA *villa){
  assert(villa);
  return villa->lnum;
}


/* Get the number of the non-leaf nodes of B+ tree. */
int vlnnum(VILLA *villa){
  assert(villa);
  return villa->nnum;
}


/* Get the number of the records stored in a database. */
int vlrnum(VILLA *villa){
  assert(villa);
  return villa->rnum;
}


/* Check whether a database handle is a writer or not. */
int vlwritable(VILLA *villa){
  assert(villa);
  return villa->wmode;
}


/* Check whether a database has a fatal error or not. */
int vlfatalerror(VILLA *villa){
  assert(villa);
  return dpfatalerror(villa->depot);
}


/* Get the inode number of a database file. */
int vlinode(VILLA *villa){
  assert(villa);
  return dpinode(villa->depot);
}


/* Get the last modified time of a database. */
int vlmtime(VILLA *villa){
  assert(villa);
  return dpmtime(villa->depot);
}


/* Begin the transaction. */
int vltranbegin(VILLA *villa){
  int err, pid;
  const char *tmp;
  VLLEAF *leaf;
  VLNODE *node;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&pid, sizeof(int), NULL);
    if(leaf->dirty){
      if(!vlleafsave(villa, leaf)) err = TRUE;
    }
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    node = (VLNODE *)cbmapget(villa->nodec, (char *)&pid, sizeof(int), NULL);
    if(node->dirty){
      if(!vlnodesave(villa, node)) err = TRUE;
    }
  }
  if(!dpsetalign(villa->depot, 0)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_ROOTKEY, villa->root)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LASTKEY, villa->last)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LNUMKEY, villa->lnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_NNUMKEY, villa->nnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_RNUMKEY, villa->rnum)) err = TRUE;
  if(!dpmemsync(villa->depot)) err = TRUE;
  villa->tran = TRUE;
  villa->rbroot = villa->root;
  villa->rblast = villa->last;
  villa->rblnum = villa->lnum;
  villa->rbnnum = villa->nnum;
  villa->rbrnum = villa->rnum;
  return err ? FALSE : TRUE;
}


/* Commit the transaction. */
int vltrancommit(VILLA *villa){
  int err, pid;
  const char *tmp;
  VLLEAF *leaf;
  VLNODE *node;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(!villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&pid, sizeof(int), NULL);
    if(leaf->dirty){
      if(!vlleafsave(villa, leaf)) err = TRUE;
    }
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    node = (VLNODE *)cbmapget(villa->nodec, (char *)&pid, sizeof(int), NULL);
    if(node->dirty){
      if(!vlnodesave(villa, node)) err = TRUE;
    }
  }
  if(!dpsetalign(villa->depot, 0)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_ROOTKEY, villa->root)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LASTKEY, villa->last)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_LNUMKEY, villa->lnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_NNUMKEY, villa->nnum)) err = TRUE;
  if(!vldpputnum(villa->depot, VL_RNUMKEY, villa->rnum)) err = TRUE;
  if(!dpmemsync(villa->depot)) err = TRUE;
  villa->tran = FALSE;
  villa->rbroot = -1;
  villa->rblast = -1;
  villa->rblnum = -1;
  villa->rbnnum = -1;
  villa->rbrnum = -1;
  while(cbmaprnum(villa->leafc) > villa->leafcnum || cbmaprnum(villa->nodec) > villa->nodecnum){
    if(!vlcacheadjust(villa)){
      err = TRUE;
      break;
    }
  }
  return err ? FALSE : TRUE;
}


/* Abort the transaction. */
int vltranabort(VILLA *villa){
  int err, pid;
  const char *tmp;
  VLLEAF *leaf;
  VLNODE *node;
  assert(villa);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(!villa->tran){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  err = FALSE;
  cbmapiterinit(villa->leafc);
  while((tmp = cbmapiternext(villa->leafc, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!(leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&pid, sizeof(int), NULL))){
      err = TRUE;
      continue;
    }
    if(leaf->dirty){
      leaf->dirty = FALSE;
      if(!vlleafcacheout(villa, pid)) err = TRUE;
    }
  }
  cbmapiterinit(villa->nodec);
  while((tmp = cbmapiternext(villa->nodec, NULL)) != NULL){
    pid = *(int *)tmp;
    if(!(node = (VLNODE *)cbmapget(villa->nodec, (char *)&pid, sizeof(int), NULL))){
      err = TRUE;
      continue;
    }
    if(node->dirty){
      node->dirty = FALSE;
      if(!vlnodecacheout(villa, pid)) err = TRUE;
    }
  }
  villa->tran = FALSE;
  villa->root = villa->rbroot;
  villa->last = villa->rblast;
  villa->lnum = villa->rblnum;
  villa->nnum = villa->rbnnum;
  villa->rnum = villa->rbrnum;
  while(cbmaprnum(villa->leafc) > villa->leafcnum || cbmaprnum(villa->nodec) > villa->nodecnum){
    if(!vlcacheadjust(villa)){
      err = TRUE;
      break;
    }
  }
  return err ? FALSE : TRUE;
}


/* Remove a database file. */
int vlremove(const char *name){
  assert(name);
  return dpremove(name);
}


/* Repair a broken database file. */
int vlrepair(const char *name, VLCFUNC cmp){
  DEPOT *depot;
  VILLA *tvilla;
  char path[VL_PATHBUFSIZ], *kbuf, *vbuf, *zbuf, *rp, *tkbuf, *tvbuf;
  int i, err, flags, omode, ksiz, vsiz, zsiz, size, step, tksiz, tvsiz, vnum;
  assert(name && cmp);
  err = FALSE;
  if(!dprepair(name)) err = TRUE;
  if(!(depot = dpopen(name, DP_OREADER, -1))) return FALSE;
  flags = dpgetflags(depot);
  if(!(flags & VL_FLISVILLA)){
    dpclose(depot);
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return FALSE;
  }
  sprintf(path, "%s%s", name, VL_TMPFSUF);
  omode = VL_OWRITER | VL_OCREAT | VL_OTRUNC;
  if(flags & VL_FLISZLIB) omode |= VL_OZCOMP;
  if(!(tvilla = vlopen(path, omode, cmp))){
    dpclose(depot);
    return FALSE;
  }
  if(!dpiterinit(depot)) err = TRUE;
  while((kbuf =  dpiternext(depot, &ksiz)) != NULL){
    if(ksiz == sizeof(int) && *(int *)kbuf < VL_NODEIDMIN && *(int *)kbuf > 0){
      if((vbuf = dpget(depot, (char *)kbuf, sizeof(int), 0, -1, &vsiz)) != NULL){
        if(_qdbm_inflate && (flags & VL_FLISZLIB) &&
           (zbuf = _qdbm_inflate(vbuf, vsiz, &zsiz)) != NULL){
          free(vbuf);
          vbuf = zbuf;
          vsiz = zsiz;
        }
        rp = vbuf;
        size = vsiz;
        if(size >= 1){
          vlreadvnumbuf(rp, size, &step);
          rp += step;
          size -= step;
        }
        if(size >= 1){
          vlreadvnumbuf(rp, size, &step);
          rp += step;
          size -= step;
        }
        while(size >= 1){
          tksiz = vlreadvnumbuf(rp, size, &step);
          rp += step;
          size -= step;
          if(size < tksiz) break;
          tkbuf = rp;
          rp += tksiz;
          size -= tksiz;
          if(size < 1) break;
          vnum = vlreadvnumbuf(rp, size, &step);
          rp += step;
          size -= step;
          if(vnum < 1 || size < 1) break;
          for(i = 0; i < vnum && size >= 1; i++){
            tvsiz = vlreadvnumbuf(rp, size, &step);
            rp += step;
            size -= step;
            if(size < tvsiz) break;
            tvbuf = rp;
            rp += tvsiz;
            size -= tvsiz;
            if(!vlput(tvilla, tkbuf, tksiz, tvbuf, tvsiz, VL_DDUP)) err = TRUE;
          }
        }
        free(vbuf);
      }
    }
    free(kbuf);
  }
  if(!vlclose(tvilla)) err = TRUE;
  if(!dpclose(depot)) err = TRUE;
  if(rename(path, name) == -1){
    if(!err) dpecodeset(DP_EMISC, __FILE__, __LINE__);
    err = TRUE;
  }
  return err ? FALSE : TRUE;
}


/* Dump all records as endian independent data. */
int vlexportdb(VILLA *villa, const char *name){
  DEPOT *depot;
  char path[VL_PATHBUFSIZ], *kbuf, *vbuf, *nkey;
  int i, err, ksiz, vsiz, ki;
  assert(villa && name);
  sprintf(path, "%s%s", name, VL_TMPFSUF);
  if(!(depot = dpopen(path, DP_OWRITER | DP_OCREAT | DP_OTRUNC, -1))) return FALSE;
  err = FALSE;
  vlcurfirst(villa);
  for(i = 0; !err && (kbuf = vlcurkey(villa, &ksiz)) != NULL; i++){
    if((vbuf = vlcurval(villa, &vsiz)) != NULL){
      CB_MALLOC(nkey, ksiz + VL_NUMBUFSIZ);
      ki = sprintf(nkey, "%X\t", i);
      memcpy(nkey + ki, kbuf, ksiz);
      if(!dpput(depot, nkey, ki + ksiz, vbuf, vsiz, DP_DKEEP)) err = TRUE;
      free(nkey);
      free(vbuf);
    } else {
      err = TRUE;
    }
    free(kbuf);
    vlcurnext(villa);
  }
  if(!dpexportdb(depot, name)) err = TRUE;
  if(!dpclose(depot)) err = TRUE;
  if(!dpremove(path)) err = TRUE;
  return !err && !vlfatalerror(villa);
}


/* Load all records from endian independent data. */
int vlimportdb(VILLA *villa, const char *name){
  DEPOT *depot;
  char path[VL_PATHBUFSIZ], *kbuf, *vbuf, *rp;
  int err, ksiz, vsiz;
  assert(villa && name);
  if(!villa->wmode){
    dpecodeset(DP_EMODE, __FILE__, __LINE__);
    return FALSE;
  }
  if(vlrnum(villa) > 0){
    dpecodeset(DP_EMISC, __FILE__, __LINE__);
    return FALSE;
  }
  kbuf = dpname(villa->depot);
  sprintf(path, "%s%s", kbuf, VL_TMPFSUF);
  free(kbuf);
  if(!(depot = dpopen(path, DP_OWRITER | DP_OCREAT | DP_OTRUNC, -1))) return FALSE;
  err = FALSE;
  if(!dpimportdb(depot, name)) err = TRUE;
  dpiterinit(depot);
  while(!err && (kbuf = dpiternext(depot, &ksiz)) != NULL){
    if((vbuf = dpget(depot, kbuf, ksiz, 0, -1, &vsiz)) != NULL){
      if((rp = strchr(kbuf, '\t')) != NULL){
        rp++;
        if(!vlput(villa, rp, ksiz - (rp - kbuf), vbuf, vsiz, VL_DDUP)) err = TRUE;
      } else {
        dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
        err = TRUE;
      }
      free(vbuf);
    } else {
      err = TRUE;
    }
    free(kbuf);
  }
  if(!dpclose(depot)) err = TRUE;
  if(!dpremove(path)) err = TRUE;
  return !err && !vlfatalerror(villa);
}



/*************************************************************************************************
 * private objects
 *************************************************************************************************/


/* Compare keys of two records by lexical order.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int vllexcompare(const char *aptr, int asiz, const char *bptr, int bsiz){
  int i, min;
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);
  min = asiz < bsiz ? asiz : bsiz;
  for(i = 0; i < min; i++){
    if(((unsigned char *)aptr)[i] != ((unsigned char *)bptr)[i])
      return ((unsigned char *)aptr)[i] - ((unsigned char *)bptr)[i];
  }
  if(asiz == bsiz) return 0;
  return asiz - bsiz;
}


/* Compare keys of two records as native integers.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int vlintcompare(const char *aptr, int asiz, const char *bptr, int bsiz){
  int anum, bnum;
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);
  if(asiz != bsiz) return asiz - bsiz;
  anum = (asiz == sizeof(int) ? *(int *)aptr : INT_MIN);
  bnum = (bsiz == sizeof(int) ? *(int *)bptr : INT_MIN);
  return anum - bnum;
}


/* Compare keys of two records as numbers of big endian.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int vlnumcompare(const char *aptr, int asiz, const char *bptr, int bsiz){
  int i;
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);
  if(asiz != bsiz) return asiz - bsiz;
  for(i = 0; i < asiz; i++){
    if(aptr[i] != bptr[i]) return aptr[i] - bptr[i];
  }
  return 0;
}


/* Compare keys of two records as numeric strings of octal, decimal or hexadecimal.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int vldeccompare(const char *aptr, int asiz, const char *bptr, int bsiz){
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);
  return (int)(strtod(aptr, NULL) - strtod(bptr, NULL));
}


/* Store a record composed of a pair of integers.
   `depot' specifies an internal database handle.
   `knum' specifies an integer of the key.
   `vnum' specifies an integer of the value.
   The return value is true if successful, else, it is false. */
static int vldpputnum(DEPOT *depot, int knum, int vnum){
  assert(depot);
  return dpput(depot, (char *)&knum, sizeof(int), (char *)&vnum, sizeof(int), DP_DOVER);
}


/* Retrieve a record composed of a pair of integers.
   `depot' specifies an internal database handle.
   `knum' specifies an integer of the key.
   `vip' specifies the pointer to a variable to assign the result to.
   The return value is true if successful, else, it is false. */
static int vldpgetnum(DEPOT *depot, int knum, int *vnp){
  char *vbuf;
  int vsiz;
  assert(depot && vnp);
  vbuf = dpget(depot, (char *)&knum, sizeof(int), 0, -1, &vsiz);
  if(!vbuf || vsiz != sizeof(int)){
    free(vbuf);
    return FALSE;
  }
  *vnp = *(int *)vbuf;
  free(vbuf);
  return TRUE;
}


/* Set a buffer for a variable length number.
   `buf' specifies the pointer to the buffer.
   `num' specifies the number.
   The return value is the size of valid region. */
static int vlsetvnumbuf(char *buf, int num){
  div_t d;
  int len;
  assert(buf && num >= 0);
  if(num == 0){
    ((signed char *)buf)[0] = 0;
    return 1;
  }
  len = 0;
  while(num > 0){
    d = div(num, 128);
    num = d.quot;
    ((signed char *)buf)[len] = d.rem;
    if(num > 0) ((signed char *)buf)[len] = -(((signed char *)buf)[len]) - 1;
    len++;
  }
  return len;
}


/* Read a variable length buffer.
   `buf' specifies the pointer to the buffer.
   `size' specifies the limit size to read.
   `sp' specifies the pointer to a variable to which the size of the read region assigned.
   The return value is the value of the buffer. */
static int vlreadvnumbuf(const char *buf, int size, int *sp){
  int i, num, base;
  assert(buf && size > 0 && sp);
  num = 0;
  base = 1;
  if(size < 2){
    *sp = 1;
    return ((signed char *)buf)[0];
  }
  for(i = 0; i < size; i++){
    if(((signed char *)buf)[i] >= 0){
      num += ((signed char *)buf)[i] * base;
      break;
    }
    num += base * (((signed char *)buf)[i] + 1) * -1;
    base *= 128;
  }
  *sp = i + 1;
  return num;
}


/* Create a new leaf.
   `villa' specifies a database handle.
   `prev' specifies the ID number of the previous leaf.
   `next' specifies the ID number of the previous leaf.
   The return value is a handle of the leaf. */
static VLLEAF *vlleafnew(VILLA *villa, int prev, int next){
  VLLEAF lent;
  assert(villa);
  lent.id = villa->lnum + VL_LEAFIDMIN;
  lent.dirty = TRUE;
  lent.recs = cblistopen();
  lent.prev = prev;
  lent.next = next;
  villa->lnum++;
  cbmapput(villa->leafc, (char *)&(lent.id), sizeof(int), (char *)&lent, sizeof(VLLEAF), TRUE);
  return (VLLEAF *)cbmapget(villa->leafc, (char *)&(lent.id), sizeof(int), NULL);
}


/* Remove a leaf from the cache.
   `villa' specifies a database handle.
   `id' specifies the ID number of the leaf.
   The return value is true if successful, else, it is false. */
static int vlleafcacheout(VILLA *villa, int id){
  VLLEAF *leaf;
  VLREC *recp;
  int i, j, err, ln;
  assert(villa && id >= VL_LEAFIDMIN);
  if(!(leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&id, sizeof(int), NULL))) return FALSE;
  err = FALSE;
  if(leaf->dirty){
    if(!vlleafsave(villa, leaf)) err = TRUE;
  }
  ln = CB_LISTNUM(leaf->recs);
  for(i = 0; i < ln; i++){
    recp = (VLREC *)CB_LISTVAL(leaf->recs, i, NULL);
    cbdatumclose(recp->key);
    cbdatumclose(recp->first);
    if(recp->rest){
      for(j = 0; j < CB_LISTNUM(recp->rest); j++){
        free(cblistpop(recp->rest, NULL));
      }
      cblistclose(recp->rest);
    }
  }
  cblistclose(leaf->recs);
  cbmapout(villa->leafc, (char *)&id, sizeof(int));
  return err ? FALSE : TRUE;
}


/* Save a leaf into the database.
   `villa' specifies a database handle.
   `leaf' specifies a leaf handle.
   The return value is true if successful, else, it is false. */
static int vlleafsave(VILLA *villa, VLLEAF *leaf){
  CBDATUM *buf;
  char vnumbuf[VL_VNUMBUFSIZ], *zbuf;
  const char *vbuf;
  VLREC *recp;
  int i, j, ksiz, vnum, vsiz, prev, next, vnumsiz, ln, zsiz;
  assert(villa && leaf);
  buf =  cbdatumopen(NULL, 0);
  prev = leaf->prev;
  if(prev == -1) prev = VL_NODEIDMIN - 1;
  vnumsiz = vlsetvnumbuf(vnumbuf, prev);
  cbdatumcat(buf, vnumbuf, vnumsiz);
  next = leaf->next;
  if(next == -1) next = VL_NODEIDMIN - 1;
  vnumsiz = vlsetvnumbuf(vnumbuf, next);
  cbdatumcat(buf, vnumbuf, vnumsiz);
  ln = CB_LISTNUM(leaf->recs);
  for(i = 0; i < ln; i++){
    recp = (VLREC *)CB_LISTVAL(leaf->recs, i, NULL);
    ksiz = CB_DATUMSIZE(recp->key);
    vnumsiz = vlsetvnumbuf(vnumbuf, ksiz);
    cbdatumcat(buf, vnumbuf, vnumsiz);
    cbdatumcat(buf, CB_DATUMPTR(recp->key), ksiz);
    vnum = 1 + (recp->rest ? CB_LISTNUM(recp->rest) : 0);
    vnumsiz = vlsetvnumbuf(vnumbuf, vnum);
    cbdatumcat(buf, vnumbuf, vnumsiz);
    vsiz = CB_DATUMSIZE(recp->first);
    vnumsiz = vlsetvnumbuf(vnumbuf, vsiz);
    cbdatumcat(buf, vnumbuf, vnumsiz);
    cbdatumcat(buf, CB_DATUMPTR(recp->first), vsiz);
    if(recp->rest){
      for(j = 0; j < CB_LISTNUM(recp->rest); j++){
        vbuf = CB_LISTVAL2(recp->rest, j, &vsiz);
        vnumsiz = vlsetvnumbuf(vnumbuf, vsiz);
        cbdatumcat(buf, vnumbuf, vnumsiz);
        cbdatumcat(buf, vbuf, vsiz);
      }
    }
  }
  if(_qdbm_deflate && villa->zmode){
    if(!(zbuf = _qdbm_deflate(CB_DATUMPTR(buf), CB_DATUMSIZE(buf), &zsiz))){
      cbdatumclose(buf);
      if(dpecode == DP_EMODE) dpecodeset(DP_EMISC, __FILE__, __LINE__);
      return FALSE;
    }
    villa->avglsiz = (villa->avglsiz * 9 + zsiz) / 10;
    if(!dpsetalign(villa->depot, (int)(villa->avglsiz * VL_ALIGNRATIO)) ||
       !dpput(villa->depot, (char *)&(leaf->id), sizeof(int), zbuf, zsiz, DP_DOVER)){
      cbdatumclose(buf);
      if(dpecode == DP_EMODE) dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return FALSE;
    }
    free(zbuf);
  } else {
    villa->avglsiz = (villa->avglsiz * 9 + CB_DATUMSIZE(buf)) / 10;
    if(!dpsetalign(villa->depot, (int)(villa->avglsiz * VL_ALIGNRATIO)) ||
       !dpput(villa->depot, (char *)&(leaf->id), sizeof(int),
              CB_DATUMPTR(buf), CB_DATUMSIZE(buf), DP_DOVER)){
      cbdatumclose(buf);
      if(dpecode == DP_EMODE) dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      return FALSE;
    }
  }
  cbdatumclose(buf);
  leaf->dirty = FALSE;
  return TRUE;
}


/* Load a leaf from the database.
   `villa' specifies a database handle.
   `id' specifies the ID number of the leaf.
   If successful, the return value is the pointer to the leaf, else, it is `NULL'. */
static VLLEAF *vlleafload(VILLA *villa, int id){
  char *buf, *rp, *kbuf, *vbuf, *zbuf;
  int i, size, step, ksiz, vnum, vsiz, prev, next, zsiz;
  VLLEAF *leaf, lent;
  VLREC rec;
  assert(villa && id >= VL_LEAFIDMIN);
  if((leaf = (VLLEAF *)cbmapget(villa->leafc, (char *)&id, sizeof(int), NULL)) != NULL){
    cbmapmove(villa->leafc, (char *)&id, sizeof(int), FALSE);
    return leaf;
  }
  ksiz = -1;
  prev = -1;
  next = -1;
  if(!(buf = dpget(villa->depot, (char *)&id, sizeof(int), 0, -1, &size))){
    dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return NULL;
  }
  if(_qdbm_inflate && villa->zmode){
    if(!(zbuf = _qdbm_inflate(buf, size, &zsiz))){
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      free(buf);
      return NULL;
    }
    free(buf);
    buf = zbuf;
    size = zsiz;
  }
  rp = buf;
  if(size >= 1){
    prev = vlreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
    if(prev >= VL_NODEIDMIN - 1) prev = -1;
  }
  if(size >= 1){
    next = vlreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
    if(next >= VL_NODEIDMIN - 1) next = -1;
  }
  lent.id = id;
  lent.dirty = FALSE;
  lent.recs = cblistopen();
  lent.prev = prev;
  lent.next = next;
  while(size >= 1){
    ksiz = vlreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
    if(size < ksiz) break;
    kbuf = rp;
    rp += ksiz;
    size -= ksiz;
    vnum = vlreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
    if(vnum < 1 || size < 1) break;
    for(i = 0; i < vnum && size >= 1; i++){
      vsiz = vlreadvnumbuf(rp, size, &step);
      rp += step;
      size -= step;
      if(size < vsiz) break;
      vbuf = rp;
      rp += vsiz;
      size -= vsiz;
      if(i < 1){
        rec.key = cbdatumopen(kbuf, ksiz);
        rec.first = cbdatumopen(vbuf, vsiz);
        rec.rest = NULL;
      } else {
        if(!rec.rest) rec.rest = cblistopen();
        cblistpush(rec.rest, vbuf, vsiz);
      }
    }
    if(i > 0) cblistpush(lent.recs, (char *)&rec, sizeof(VLREC));
  }
  free(buf);
  cbmapput(villa->leafc, (char *)&(lent.id), sizeof(int), (char *)&lent, sizeof(VLLEAF), TRUE);
  return (VLLEAF *)cbmapget(villa->leafc, (char *)&(lent.id), sizeof(int), NULL);
}


/* Add a record to a leaf.
   `villa' specifies a database handle.
   `leaf' specifies a leaf handle.
   `dmode' specifies behavior when the key overlaps.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region of the value.
   The return value is true if successful, else, it is false. */
static int vlleafaddrec(VILLA *villa, VLLEAF *leaf, int dmode,
                        const char *kbuf, int ksiz, const char *vbuf, int vsiz){
  VLREC *recp, rec;
  int i, rv, left, right, ln;
  assert(villa && leaf && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  left = 0;
  ln = CB_LISTNUM(leaf->recs);
  right = ln;
  i = (left + right) / 2;
  while(right >= left && i < ln){
    recp = (VLREC *)CB_LISTVAL(leaf->recs, i, NULL);
    rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key));
    if(rv == 0){
      break;
    } else if(rv <= 0){
      right = i - 1;
    } else {
      left = i + 1;
    }
    i = (left + right) / 2;
  }
  while(i < ln){
    recp = (VLREC *)CB_LISTVAL(leaf->recs, i, NULL);
    rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key));
    if(rv == 0){
      switch(dmode){
      case VL_DOVER:
        cbdatumclose(recp->first);
        recp->first = cbdatumopen(vbuf, vsiz);
        break;
      case VL_DKEEP:
        return FALSE;
      default:
        if(!recp->rest) recp->rest = cblistopen();
        cblistpush(recp->rest, vbuf, vsiz);
        villa->rnum++;
        break;
      }
      break;
    } else if(rv < 0){
      rec.key = cbdatumopen(kbuf, ksiz);
      rec.first = cbdatumopen(vbuf, vsiz);
      rec.rest = NULL;
      cblistinsert(leaf->recs, i, (char *)&rec, sizeof(VLREC));
      villa->rnum++;
      break;
    }
    i++;
  }
  if(i >= ln){
    rec.key = cbdatumopen(kbuf, ksiz);
    rec.first = cbdatumopen(vbuf, vsiz);
    rec.rest = NULL;
    cblistpush(leaf->recs, (char *)&rec, sizeof(VLREC));
    villa->rnum++;
  }
  leaf->dirty = TRUE;
  return TRUE;
}


/* Divide a leaf into two.
   `villa' specifies a database handle.
   `leaf' specifies a leaf handle.
   The return value is the handle of a new leaf, or `NULL' on failure. */
static VLLEAF *vlleafdivide(VILLA *villa, VLLEAF *leaf){
  VLLEAF *newleaf, *nextleaf;
  VLREC *recp;
  int i, mid, ln;
  assert(villa && leaf);
  mid = CB_LISTNUM(leaf->recs) / 2;
  recp = (VLREC *)CB_LISTVAL(leaf->recs, mid, NULL);
  newleaf = vlleafnew(villa, leaf->id, leaf->next);
  if(newleaf->next != -1){
    if(!(nextleaf = vlleafload(villa, newleaf->next))) return NULL;
    nextleaf->prev = newleaf->id;
    nextleaf->dirty = TRUE;
  }
  leaf->next = newleaf->id;
  leaf->dirty = TRUE;
  ln = CB_LISTNUM(leaf->recs);
  for(i = mid; i < ln; i++){
    recp = (VLREC *)CB_LISTVAL(leaf->recs, i, NULL);
    cblistpush(newleaf->recs, (char *)recp, sizeof(VLREC));
  }
  ln = CB_LISTNUM(newleaf->recs);
  for(i = 0; i < ln; i++){
    free(cblistpop(leaf->recs, NULL));
  }
  return newleaf;
}


/* Create a new node.
   `villa' specifies a database handle.
   `id' specifies the ID number of the node.
   The return value is a handle of the node. */
static VLNODE *vlnodenew(VILLA *villa, int heir){
  VLNODE nent;
  assert(villa && heir >= VL_LEAFIDMIN);
  nent.id = villa->nnum + VL_NODEIDMIN;
  nent.dirty = TRUE;
  nent.heir = heir;
  nent.idxs = cblistopen();
  villa->nnum++;
  cbmapput(villa->nodec, (char *)&(nent.id), sizeof(int), (char *)&nent, sizeof(VLNODE), TRUE);
  return (VLNODE *)cbmapget(villa->nodec, (char *)&(nent.id), sizeof(int), NULL);
}


/* Remove a node from the cache.
   `villa' specifies a database handle.
   `id' specifies the ID number of the node.
   The return value is true if successful, else, it is false. */
static int vlnodecacheout(VILLA *villa, int id){
  VLNODE *node;
  VLIDX *idxp;
  int i, err, ln;
  assert(villa && id >= VL_NODEIDMIN);
  if(!(node = (VLNODE *)cbmapget(villa->nodec, (char *)&id, sizeof(int), NULL))) return FALSE;
  err = FALSE;
  if(node->dirty){
    if(!vlnodesave(villa, node)) err = TRUE;
  }
  ln = CB_LISTNUM(node->idxs);
  for(i = 0; i < ln; i++){
    idxp = (VLIDX *)CB_LISTVAL(node->idxs, i, NULL);
    cbdatumclose(idxp->key);
  }
  cblistclose(node->idxs);
  cbmapout(villa->nodec, (char *)&id, sizeof(int));
  return err ? FALSE : TRUE;
}


/* Save a node into the database.
   `villa' specifies a database handle.
   `node' specifies a node handle.
   The return value is true if successful, else, it is false. */
static int vlnodesave(VILLA *villa, VLNODE *node){
  CBDATUM *buf;
  char vnumbuf[VL_VNUMBUFSIZ];
  VLIDX *idxp;
  int i, heir, pid, ksiz, vnumsiz, ln;
  assert(villa && node);
  buf = cbdatumopen(NULL, 0);
  heir = node->heir;
  vnumsiz = vlsetvnumbuf(vnumbuf, heir);
  cbdatumcat(buf, vnumbuf, vnumsiz);
  ln = CB_LISTNUM(node->idxs);
  for(i = 0; i < ln; i++){
    idxp = (VLIDX *)CB_LISTVAL(node->idxs, i, NULL);
    pid = idxp->pid;
    vnumsiz = vlsetvnumbuf(vnumbuf, pid);
    cbdatumcat(buf, vnumbuf, vnumsiz);
    ksiz = CB_DATUMSIZE(idxp->key);
    vnumsiz = vlsetvnumbuf(vnumbuf, ksiz);
    cbdatumcat(buf, vnumbuf, vnumsiz);
    cbdatumcat(buf, CB_DATUMPTR(idxp->key), ksiz);
  }
  villa->avgnsiz = (villa->avgnsiz * 9 + CB_DATUMSIZE(buf)) / 10;
  if(!dpsetalign(villa->depot, (int)(villa->avgnsiz * VL_ALIGNRATIO)) ||
     !dpput(villa->depot, (char *)&(node->id), sizeof(int),
            CB_DATUMPTR(buf), CB_DATUMSIZE(buf), DP_DOVER)){
    cbdatumclose(buf);
    if(dpecode == DP_EMODE) dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
    return FALSE;
  }
  cbdatumclose(buf);
  node->dirty = FALSE;
  return TRUE;
}


/* Load a node from the database.
   `villa' specifies a database handle.
   `id' specifies the ID number of the node.
   If successful, the return value is the pointer to the node, else, it is `NULL'. */
static VLNODE *vlnodeload(VILLA *villa, int id){
  char *buf, *rp, *kbuf;
  int size, step, heir, pid, ksiz;
  VLNODE *node, nent;
  VLIDX idx;
  assert(villa && id >= VL_NODEIDMIN);
  if((node = (VLNODE *)cbmapget(villa->nodec, (char *)&id, sizeof(int), NULL)) != NULL){
    cbmapmove(villa->nodec, (char *)&id, sizeof(int), FALSE);
    return node;
  }
  heir = -1;
  if(!(buf = dpget(villa->depot, (char *)&id, sizeof(int), 0, -1, &size))) return NULL;
  rp = buf;
  if(size >= 1){
    heir = vlreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
  }
  if(heir < 0){
    free(buf);
    return NULL;
  }
  nent.id = id;
  nent.dirty = FALSE;
  nent.heir = heir;
  nent.idxs = cblistopen();
  while(size >= 1){
    pid = vlreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
    if(size < 1) break;
    ksiz = vlreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
    if(size < ksiz) break;
    kbuf = rp;
    rp += ksiz;
    size -= ksiz;
    idx.pid = pid;
    idx.key = cbdatumopen(kbuf, ksiz);
    cblistpush(nent.idxs, (char *)&idx, sizeof(VLIDX));
  }
  free(buf);
  cbmapput(villa->nodec, (char *)&(nent.id), sizeof(int), (char *)&nent, sizeof(VLNODE), TRUE);
  return (VLNODE *)cbmapget(villa->nodec, (char *)&(nent.id), sizeof(int), NULL);
}


/* Add an index to a node.
   `villa' specifies a database handle.
   `node' specifies a node handle.
   `order' specifies whether the calling sequence is orderd or not.
   `pid' specifies the ID number of referred page.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key. */
static void vlnodeaddidx(VILLA *villa, VLNODE *node, int order,
                         int pid, const char *kbuf, int ksiz){
  VLIDX idx, *idxp;
  int i, rv, left, right, ln;
  assert(villa && node && pid >= VL_LEAFIDMIN && kbuf && ksiz >= 0);
  idx.pid = pid;
  idx.key = cbdatumopen(kbuf, ksiz);
  if(order){
    cblistpush(node->idxs, (char *)&idx, sizeof(VLIDX));
  } else {
    left = 0;
    right = CB_LISTNUM(node->idxs);
    i = (left + right) / 2;
    ln = CB_LISTNUM(node->idxs);
    while(right >= left && i < ln){
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, i, NULL);
      rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key));
      if(rv == 0){
        break;
      } else if(rv <= 0){
        right = i - 1;
      } else {
        left = i + 1;
      }
      i = (left + right) / 2;
    }
    ln = CB_LISTNUM(node->idxs);
    while(i < ln){
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, i, NULL);
      if(villa->cmp(kbuf, ksiz, CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key)) < 0){
        cblistinsert(node->idxs, i, (char *)&idx, sizeof(VLIDX));
        break;
      }
      i++;
    }
    if(i >= CB_LISTNUM(node->idxs)) cblistpush(node->idxs, (char *)&idx, sizeof(VLIDX));
  }
  node->dirty = TRUE;
}


/* Search the leaf corresponding to a key.
   `villa' specifies a database handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.
   `hist' specifies an array history of visited nodes.  If `NULL', it is not used.
   `hnp' specifies the pointer to a variable to which the number of elements of the history
   assigned.  If `NULL', it is not used.
   The return value is the ID number of the leaf, or -1 on failure. */
static int vlsearchleaf(VILLA *villa, const char *kbuf, int ksiz, int *hist, int *hnp){
  VLNODE *node;
  VLIDX *idxp;
  int i, pid, level, rv, left, right, ln;
  assert(villa && kbuf && ksiz >= 0);
  pid = villa->root;
  idxp = NULL;
  level = 0;
  while(pid >= VL_NODEIDMIN){
    if(!(node = vlnodeload(villa, pid)) || (ln = CB_LISTNUM(node->idxs)) < 1){
      dpecodeset(DP_EBROKEN, __FILE__, __LINE__);
      if(hnp) *hnp = level;
      return -1;
    }
    if(hist) hist[level++] = node->id;
    left = 1;
    right = ln;
    i = (left + right) / 2;
    while(right >= left && i < ln){
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, i, NULL);
      rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key));
      if(rv == 0){
        break;
      } else if(rv <= 0){
        right = i - 1;
      } else {
        left = i + 1;
      }
      i = (left + right) / 2;
    }
    if(i > 0) i--;
    while(i < ln){
      idxp = (VLIDX *)CB_LISTVAL(node->idxs, i, NULL);
      if(villa->cmp(kbuf, ksiz, CB_DATUMPTR(idxp->key), CB_DATUMSIZE(idxp->key)) < 0){
        if(i == 0){
          pid = node->heir;
          break;
        }
        idxp = (VLIDX *)CB_LISTVAL(node->idxs, i - 1, NULL);
        pid = idxp->pid;
        break;
      }
      i++;
    }
    if(i >= ln) pid = idxp->pid;
  }
  if(hnp) *hnp = level;
  return pid;
}


/* Adjust the caches for leaves and nodes.
   `villa' specifies a database handle.
   The return value is true if successful, else, it is false. */
static int vlcacheadjust(VILLA *villa){
  const char *tmp;
  int i, pid, err;
  err = FALSE;
  if(cbmaprnum(villa->leafc) > villa->leafcnum){
    cbmapiterinit(villa->leafc);
    for(i = 0; i < VL_CACHEOUT; i++){
      tmp = cbmapiternext(villa->leafc, NULL);
      pid = *(int *)tmp;
      if(!vlleafcacheout(villa, pid)) err = TRUE;
    }
  }
  if(cbmaprnum(villa->nodec) > villa->nodecnum){
    cbmapiterinit(villa->nodec);
    for(i = 0; i < VL_CACHEOUT; i++){
      tmp = cbmapiternext(villa->nodec, NULL);
      pid = *(int *)tmp;
      if(!vlnodecacheout(villa, pid)) err = TRUE;
    }
  }
  return err ? FALSE : TRUE;
}


/* Search a record of a leaf.
   `villa' specifies a database handle.
   `leaf' specifies a leaf handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.
   `ip' specifies the pointer to a variable to fetch the index of the correspnding record.
   The return value is the pointer to a corresponding record, or `NULL' on failure. */
static VLREC *vlrecsearch(VILLA *villa, VLLEAF *leaf, const char *kbuf, int ksiz, int *ip){
  int i, rv, left, right, ln;
  VLREC *recp;
  assert(villa && leaf && kbuf && ksiz >= 0);
  ln = CB_LISTNUM(leaf->recs);
  left = 0;
  right = ln;
  i = (left + right) / 2;
  while(right >= left && i < ln){
    recp = (VLREC *)CB_LISTVAL(leaf->recs, i, NULL);
    rv = villa->cmp(kbuf, ksiz, CB_DATUMPTR(recp->key), CB_DATUMSIZE(recp->key));
    if(rv == 0){
      if(ip) *ip = i;
      return recp;
    } else if(rv <= 0){
      right = i - 1;
    } else {
      left = i + 1;
    }
    i = (left + right) / 2;
  }
  if(ip) *ip = i;
  return NULL;
}



/* END OF FILE */
