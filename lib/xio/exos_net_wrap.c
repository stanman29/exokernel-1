
/*
 * Copyright (C) 1997 Massachusetts Institute of Technology 
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose and without fee or royalty is
 * hereby granted, provided that the full text of this NOTICE appears on
 * ALL copies of the software and documentation or portions thereof,
 * including modifications, that you make.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED. BY WAY OF EXAMPLE,
 * BUT NOT LIMITATION, COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS OR
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR
 * THAT THE USE OF THE SOFTWARE OR DOCUMENTATION WILL NOT INFRINGE ANY
 * THIRD PARTY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS. COPYRIGHT
 * HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE OR
 * DOCUMENTATION.
 *
 * The name and trademarks of copyright holders may NOT be used in
 * advertising or publicity pertaining to the software without specific,
 * written prior permission. Title to copyright in this software and any
 * associated documentation will at all times remain with copyright
 * holders. See the file AUTHORS which should have accompanied this software
 * for a list of all copyright holders.
 *
 * This file may be derived from previously copyrighted software. This
 * copyright applies only to those changes made by the copyright
 * holders listed in the AUTHORS file. The rest of this file is covered by
 * the copyright notices, if any, listed below.
 */


#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <exos/tick.h>
#include <exos/mallocs.h>

#include <xok/sysinfo.h>
#include <xok/sys_ucall.h>
#include <exos/process.h>
#include <exos/cap.h>
#include <exos/uwk.h>
#include <xok/ae_recv.h>
#include <exos/net/ae_net.h>
#include <exos/net/ether.h>
#include <exos/osdecl.h>

#include "xio_net_wrap.h"

int inpackets = 0;
int outpackets = 0;


/********** send wrapper for abstracting interface differences ************/

int xio_net_wrap_getnetcardno (xio_nwinfo_t *nwinfo, char *eth_addr)
{
   int i;
 
   for (i=0; i<__sysinfo.si_nnetworks; i++) {
      if (bcmp (eth_addr, __sysinfo.si_networks[i].ether_addr, 6) == 0) {
	 return (i);
      }
   }
 
   for (i=0; i<6; i++) {
      if (eth_addr[i]) {
	 return (-1);
      }
   }
 
   return (0);
}


/* Most of the xoknet interfaces copies data from the user-supplied	  */
/* buffers immediately, so no wrapping is necessary.  The de interface	  */
/* (for the tulip card) uses DMA at the time of the actual trasnmission.  */
/* As a result, the buffers passed thru the send interface must remain	  */
/* inviolate until the transmission has occurred.  The de interface	  */
/* asynchronously notifies us of this fact by decrementing an integer	  */
/* (we pass it a pointer to this integer).				  */

/* So, most of this section of code is for handling the de interface	  */
/* semantics.  We do this by allowing the sender to identify how much of  */
/* the packet is "volatile" and assuming that the remainder is inviolate. */
/* (Note: the caller is responsible for making certain that this is the   */
/* case.)  A dedicated bit of memory is used as a circular buffer to hold */
/* copies of transient data and small structures (dmaholder) to support   */
/* reclaim.								  */

typedef struct dmaholder {
   struct dmaholder *next;
   int pending;
   int end;
   char data[4];
} dmaholder;

static dmaholder *dmaholders = NULL;
static dmaholder *prevhead = NULL;

#define SENDBUFLEN 65536
static char *send_buf = NULL;
static int freehead = 0;
static int freetail = SENDBUFLEN;

int xio_net_wrap_guaranteedReceive = 1;

/* help for testing timeouts and retransmission */
/* #define TESTDROP		/* Enable test-assist packet dropping */
#ifdef TESTDROP
static int dropperc = 0;
static int droppedlast = 0;
#endif

int xio_net_wrap_send (int netcardno, struct ae_recv *send_recv, int copysz)
{
   struct ae_recv temp_recv;
   dmaholder *newdma;
   int zerosz = 0;
   int i, sz;
   int ret;
/*
kprintf ("xio_net_send_wrap: netcardno %d, send_recv %p, copysz %d\n", netcardno, send_recv, copysz);
*/

#ifdef TESTDROP
   /* Check whether we should drop this packet for testing purposes */
   {  long r;
      r = random();
      if ((droppedlast == 0) && ((r % 100) < dropperc)) {
	 droppedlast = 1;
	 STINC(ndropsnd);
	 ret = 0;
	 goto send_done;
      } else {
	 droppedlast = 0;
      }
   }
#endif

   /* XXX -- do we even need this check now that sys_net_xmit handles
      all ethernet card types? */

   /* Most interfaces don't need any of this */
   if (__sysinfo.si_networks[netcardno].cardtype != XOKNET_DE) {
      ret = ae_eth_sendv (send_recv, netcardno);
      goto send_done;
   }

   if (send_buf == NULL) {
      send_buf = __malloc (SENDBUFLEN);
      assert (send_buf != NULL);
   }

   /* reclaim space holding transmitted data */
   while ((prevhead) && (prevhead->next->pending <= 0)) {
      assert (prevhead->next->pending == 0);
      newdma = prevhead->next;
      prevhead->next = newdma->next;
      prevhead = (newdma != prevhead) ? prevhead : NULL;
      assert (newdma->end > freetail);
      freetail = newdma->end;
/*
kprintf ("reclaiming a dmaholder from prevlist: newdma %p, prevhead %p, &send_buf[0] %p\n", newdma, prevhead, &send_buf[0]);
*/
   }
   /* This loop should only be entered when on "first-time" thru send_buf. */
   /* (i.e., when no previous time thru still has buffered packet parts)   */
   while ((dmaholders) && (dmaholders->next->pending <= 0)) {
      assert ((prevhead == NULL) && (dmaholders->next->pending == 0));
      newdma = dmaholders->next;
      dmaholders->next = newdma->next;
      dmaholders = (newdma != dmaholders) ? dmaholders : NULL;
      /* no adjustment of freetail is appropriate, since still behind it... */
/*
kprintf ("reclaiming a dmaholder: newdma %p, dmaholders %p, &send_buf[0] %p\n", newdma, dmaholders, &send_buf[0]);
*/
   }

   /* reset if nothing pending */
   if (dmaholders == NULL) {
      assert (prevhead == NULL);
      freehead = 0;
      freetail = SENDBUFLEN;
   }

   /* Make certain that packet will be minimum ethernet size (de interface */
   /* will reject it otherwise).					   */
   if (ae_recv_datacnt(send_recv) < ETHER_MIN_LEN) {
      zerosz = ETHER_MIN_LEN - ae_recv_datacnt(send_recv);
      copysz = max (ETHER_MIN_LEN, copysz);
   }

   /* Find free space for the transient send data (default is freehead). */
   /* (Note that (copysz + sizeof(dmaholder)) is slightly conservative.) */
   while ((freetail - freehead) < (copysz + sizeof(dmaholder))) {
/*
kprintf ("at end of buffer.  try it from the beginning: dmaholders %p (pend %d)\n", dmaholders->next, dmaholders->next->pending);
kprintf ("freehead %d, freetail %d\n", freehead, freetail);
*/
      if (prevhead) {
	 return (-1);
      }
      freehead = 0;
      freetail = (uint)dmaholders->next - (uint)&send_buf[0];
      prevhead = dmaholders;
      dmaholders = NULL;
   }

   /* initialize the data structure and add to the tail-pointed circle-q */
   newdma = (dmaholder *) &send_buf[freehead];
   newdma->pending = 1;
   ret = ae_recv_datacpy (send_recv, newdma->data, 0, copysz);
   assert (ret <= copysz);

   /* update freehead to reflect space used */
   freehead += roundup ((sizeof(dmaholder)+copysz), sizeof(int));
   newdma->end = freehead;

	/* Insert ordered to list */
   assert ((uint)newdma > (uint)dmaholders);
   if (dmaholders == NULL) {
      newdma->next = newdma;
   } else {
      newdma->next = dmaholders->next;
      dmaholders->next = newdma;
   }
   dmaholders = newdma;

   /* reset recv to point to steady version of transient data, call send, */
   /* and restore the pointers. */
   temp_recv.n = 1;
   temp_recv.r[0].data = newdma->data;
   temp_recv.r[0].sz = copysz;
   sz = 0;
   for (i=0; i<send_recv->n; i++) {
      if ((sz + send_recv->r[i].sz) > copysz) {
	 int off = max (0, (copysz-sz));
	 assert (temp_recv.n < 8);
	 temp_recv.r[temp_recv.n].data = &send_recv->r[i].data[off];
	 temp_recv.r[temp_recv.n].sz = send_recv->r[i].sz - off;
	 temp_recv.n++;
      }
      sz += send_recv->r[i].sz;
   }
   if (zerosz) {
      assert (temp_recv.n == 1);
      bzero (&temp_recv.r[0].data[temp_recv.r[0].sz], zerosz);
   }
   ret = sys_net_xmit (netcardno, &temp_recv, &newdma->pending, (xio_net_wrap_guaranteedReceive == 0));
   assert (ret == 0);

send_done:
   if (ret == 0) {
      outpackets++;
   }
   return (ret);
}


/*************** functions for incoming data buffer management ****************/

void xio_net_wrap_keepPacket (xio_nwinfo_t *nwinfo, struct ae_recv *recv)
{
   xio_net_buf_t *steal = recvtopollbuf (recv);
   xio_net_buf_t *replacebuf;
   xio_net_buf_t *prevbuf;
   int ringid = nwinfo->ringid;

kprintf ("keepPacket\n");

   prevbuf = nwinfo->pollbuflist;
   do {
      if (prevbuf->next == steal) {
	 break;
      }
      prevbuf = prevbuf->next;
   } while (prevbuf != nwinfo->pollbuflist);

   if (prevbuf->next != steal) {
      kprintf ("xio_net_wrap_stealpollbuf: stealing a pollbuf that doesn't exist\n");
      assert (0);
   }

   replacebuf = nwinfo->extrapollbufs;
   nwinfo->extrapollbufs = replacebuf->next;

   if (nwinfo->pollbuftail == steal) {
      nwinfo->pollbuftail = replacebuf;
   } else if (nwinfo->pollbuflist == steal) {
      nwinfo->pollbuflist = replacebuf;
   }
/*
kprintf ("before modring: prev %p, curr %p, next %p\n", prevbuf, prevbuf->next, prevbuf->next->next);
*/
   replacebuf->next = steal->next;
   steal->next = NULL;
   prevbuf->next = replacebuf;
   replacebuf->poll = 0;	/* give space to kernel */
   assert (replacebuf->n == 1);
   assert (replacebuf->sz == ETHER_MAX_LEN);

	/* replace stolen buffer with replacement in app/kernel ring */
   if (sys_pktring_modring (CAP_ROOT, ringid, (struct pktringent *)replacebuf, (struct pktringent *)steal, 1) < 0) {
      kprintf ("xio_net_wrap_stealpollbuf: modring failed\n");
      assert (0);
   }
/*
kprintf ("after modring (pollbuflist %p): prev %p, curr %p, next %p\n", nwinfo->pollbuflist, prevbuf, prevbuf->next, prevbuf->next->next);
*/
}


void xio_net_wrap_returnPacket (xio_nwinfo_t *nwinfo, struct ae_recv *recv)
{
   xio_net_buf_t *retbuf = recvtopollbuf (recv);

   recv->r[0].sz = ETHER_MAX_LEN;
   assert (recv->r[0].data == (uint8 *)&retbuf->space[2]);

   if (retbuf->next) {
      retbuf->poll = 0;

   } else {
	/* later, add buffer to app/kernel ring if dropping packets! */
      retbuf->next = nwinfo->extrapollbufs;
      nwinfo->extrapollbufs = retbuf;
   }
}


int xio_net_wrap_checkforPacket (xio_nwinfo_t *nwinfo)
{
   return (nwinfo->pollbuflist->poll > 0);
}


void xio_net_wrap_printPacket (xio_nwinfo_t *nwinfo)
{
   kprintf ("(%d %d) ringid %d, nextPacket %p, readylen %d (phys %x)\n", getpid(), __envid, nwinfo->ringid, nwinfo->pollbuflist, nwinfo->pollbuflist->poll, vpt[PGNO(((uint)nwinfo->pollbuflist))]);
}


struct ae_recv * xio_net_wrap_getPacket (xio_nwinfo_t *nwinfo)
{
   if (nwinfo->pollbuflist->poll > 0) {
      xio_net_buf_t *tmpbuf = nwinfo->pollbuflist;
      nwinfo->pollbuflist = nwinfo->pollbuflist->next;
      assert (tmpbuf->n == 1);
      tmpbuf->sz = tmpbuf->poll;
      inpackets++;
/*
kprintf ("(%d %d) xio_net_wrap_getPacket: returning %p for buffer %p [phys %x] (poll %d)\n", getpid(), __envid, &tmpbuf->n, tmpbuf, tmpbuf->poll, vpt[PGNO(((uint)tmpbuf))]);
*/
      return ((struct ae_recv *) &tmpbuf->n);
   }
/*
kprintf ("xio_net_wrap_getPacket: returning NULL (pollbuf %p)\n", nwinfo->pollbuflist);
*/
   return (NULL);
}


void xio_net_wrap_waitforPacket (xio_nwinfo_t *nwinfo)
{
   if (nwinfo->pollbuflist->poll == 0) {
      struct wk_term t[XIO_WAITFORPACKET_PREDLEN];
      int sz = xio_net_wrap_wkpred_packetarrival (nwinfo, &t[0]);
      //kprintf ("xio_net_wrap_waitforPacket: poll %d, sz %d\n", nwinfo->pollbuflist->poll, sz);
      if (sz > 0) {
	 wk_waitfor_pred (&t[0], sz);
      }
   }
}

int xio_net_wrap_init (xio_nwinfo_t *nwinfo, char *bufferSpace, uint size)
{
   int i;
   int numpollbufs = size / sizeof (xio_net_buf_t);
   xio_net_buf_t *newpollbufs;
   int ringid = 0;

   nwinfo->pollbuflist = NULL;
   nwinfo->pollbuftail = NULL;
   nwinfo->extrapollbufs = NULL;
   nwinfo->ringid = 0;

   if (size == 0) {
      return (0);
   }

#if 0
kprintf ("(%d %d) xio_net_wrap_init: bufferSpace %p [phys %x], size %d\n", getpid(), __envid, bufferSpace, vpt[PGNO(((uint)bufferSpace))], size);
#endif

   assert (size >= 0);
   assert (bufferSpace != NULL);

   newpollbufs = (xio_net_buf_t *) bufferSpace;
   for (i=0; i<numpollbufs; i++) {
      newpollbufs[i].poll = 0;
      newpollbufs[i].pollptr = (uint *) &(newpollbufs[i].poll);
      newpollbufs[i].n = 1;
      newpollbufs[i].sz = ETHER_MAX_LEN;
      newpollbufs[i].data = &(newpollbufs[i].space[2]);
      newpollbufs[i].next = &(newpollbufs[(i+1)]);
   }
   newpollbufs[(numpollbufs-1)].next = &(newpollbufs[0]);
   nwinfo->pollbuflist = newpollbufs;
   nwinfo->pollbuftail = &(newpollbufs[(numpollbufs-1)]);
   assert (nwinfo->pollbuftail->next == nwinfo->pollbuflist);
   if ((ringid = sys_pktring_setring (CAP_ROOT, 0, (struct pktringent *) nwinfo->pollbuflist)) <= 0) {
      kprintf ("setring failed: no soup for you!\n");
      assert (0);
   }
   nwinfo->ringid = ringid;

#if 0
kprintf ("(%d %d) init done: pollbuflist %p [phys %x], readylen %d, ringid %d\n", getpid(), __envid, nwinfo->pollbuflist, vpt[PGNO(((uint)nwinfo->pollbuflist))], nwinfo->pollbuflist->poll, nwinfo->ringid);
#endif

   return (ringid);
}


void xio_net_wrap_giveBufferSpace (xio_nwinfo_t *nwinfo, char *bufferSpace, uint size)
{
   assert (size >= 0);
   assert (bufferSpace != NULL);
   assert (0);	/* not yet implemented */
}


void xio_net_wrap_shutdown (xio_nwinfo_t *nwinfo)
{
   if (nwinfo->ringid) {
      sys_pktring_delring (CAP_ROOT, nwinfo->ringid);
   }
}


void xio_net_wrap_setdroprate (xio_nwinfo_t *nwinfo, int perc)
{
#ifdef TESTDROP
   dropperc = perc;
#endif
}


int xio_net_wrap_wkpred_packetarrival (xio_nwinfo_t *nwinfo, struct wk_term *t)
{
   int next = 0;
/*
kprintf ("xio_net_wrap_wkpred_packetarrival: pollbuflist %p, poll %d\n", nwinfo->pollbuflist, nwinfo->pollbuflist->poll);
*/
   if (nwinfo->pollbuflist->poll > 0) {
      return (-1);
   }

   next = wk_mkvar (next, t, wk_phys (&(nwinfo->pollbuflist->poll)), CAP_ROOT);
   next = wk_mkimm (next, t, 0);
   next = wk_mkop (next, t, WK_NEQ);
	/* now, in case shared... */
   next = wk_mkop (next, t, WK_OR);
   next = wk_mkvar (next, t, wk_phys(&(nwinfo->pollbuflist)), CAP_ROOT);
   next = wk_mkimm (next, t, (uint) nwinfo->pollbuflist);
   next = wk_mkop (next, t, WK_NEQ);

   assert (next <= XIO_WAITFORPACKET_PREDLEN);

   return (next);
}


#include <dpf/dpf.h>
#include <dpf/dpf-internal.h>
#include <exos/netinet/fast_eth.h>
#include <exos/netinet/fast_ip.h>

int xio_net_wrap_getdpf_udp (xio_nwinfo_t *nwinfo, int srcportno, int dstportno)
{
   struct dpf_ir ir;
   int demux_id;

   dpf_begin (&ir);					

   //dpf_eq16 (&ir, 12, htons(EP_IP));	/* must be IP packet */
   //dpf_eq8 (&ir, 23, IP_PROTO_UDP);	/* must be UDP packet */

	/* must reverse src and dst, since DPF looks at incoming packets */
   if (dstportno != -1) {
	/* 34 is offset of source UDP port! */
      dpf_eq16 (&ir, 34, htons((uint16)dstportno));
   }

   if (srcportno != -1) {
	/* 36 is offset of destination UDP port! */
      dpf_eq16 (&ir, 36, htons((uint16)srcportno));
   }

   if ((demux_id = sys_self_dpf_insert (CAP_ROOT, CAP_ROOT, &ir, nwinfo->ringid)) < 0) {
      kprintf ("Unable to install UDP filter for srcport %d and dstport %d (%d)\n", srcportno, dstportno, demux_id);
      return(-1);
   }
/*
   kprintf ("DPF FILTER #%d (ringid %d) (src %d, dst %d)\n", demux_id, nwinfo->ringid, srcportno, dstportno);
*/

   return (demux_id);
}


int xio_net_wrap_getdpf_tcp (xio_nwinfo_t *nwinfo, int srcportno, int dstportno)
{
   struct dpf_ir ir;
   int demux_id;

   dpf_begin (&ir);					

   //dpf_eq16 (&ir, 12, htons(EP_IP));	/* must be IP packet */
   //dpf_eq8 (&ir, 23, IP_PROTO_TCP);	/* must be TCP packet */

	/* must reverse src and dst, since DPF looks at incoming packets */
   if (dstportno != -1) {
	/* 34 is offset of source TCP port! */
      dpf_eq16 (&ir, 34, htons((uint16)dstportno));
   }

   if (srcportno != -1) {
	/* 36 is offset of destination TCP port! */
      dpf_eq16 (&ir, 36, htons((uint16)srcportno));
   }

   if ((demux_id = sys_self_dpf_insert (CAP_ROOT, CAP_ROOT, &ir, nwinfo->ringid)) < 0) {
      kprintf ("Unable to install TCP filter for srcport %d and dstport %d (%d)\n", srcportno, dstportno, demux_id);
      return(-1);
   }
/*
   kprintf ("DPF FILTER #%d (ringid %d) (src %d, dst %d)\n", demux_id, nwinfo->ringid, srcportno, dstportno);
*/

   return (demux_id);
}


int xio_net_wrap_getdpf_tcprange (xio_nwinfo_t *nwinfo, int firstsrcport, int lastsrcport, int firstdstport, int lastdstport)
{
	/* obviously not supported yet (in fact, DPF won't support it) */
   assert (0);
   return (0);
}


int xio_net_wrap_freedpf (xio_nwinfo_t *nwinfo, int demux_id)
{
   if (demux_id != -1) {
      return (sys_self_dpf_delete (CAP_ROOT, demux_id));
   }
   return (0);
}


int xio_net_wrap_getnettap (xio_nwinfo_t *nwinfo, u_int capno, u_int interfaces)
{
   int ret = sys_nettap (capno, nwinfo->ringid, interfaces);
   if (ret < 0) {
      kprintf ("xio_net_wrap_getnettap: Unable to install nettap on interfaces 0x%x\n", interfaces);
   }
   return (ret);
}


int xio_net_wrap_delnettap (xio_nwinfo_t *nwinfo, u_int capno, int tapno)
{
   return (sys_nettap (capno, tapno, 0));
}


int xio_net_wrap_reroutedpf (xio_nwinfo_t *nwinfo, int demux_id)
{
   int ret;
   assert (demux_id != -1);
   ret = sys_self_dpf_pktring (CAP_ROOT, demux_id, nwinfo->ringid);
   assert (ret == 0);
   return (demux_id);
}

