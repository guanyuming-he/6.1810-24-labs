#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

// Use 512 KiB to store the cached UDP packets. 
// See attack_plan.txt
static struct port_binding udp_port_bindings[65536];


// If h != t,
// then cleans current h with kfree and advances h.
// The user should have retrieved h prior to calling this, 
// if he needs that content.
void udp_cache_consume(
		struct udp_cache* uc
) {
	if (uc->h != uc->t)
	{
		// It is the ip header that's passed in,
		// but it's the eth header that's obtained from kalloc().
		kfree(uc->packets[uc->h].data - sizeof(struct eth));
		uc->h = UCACHE_INC(uc->h);
	}
}
// returns (t-h) mod N.
uint32 udp_cache_size(
		const struct udp_cache* uc
)
{
	// In C, a % b is not the mathematical modulo.
	// Thus, I make a positive first.
	return (
		((int64)uc->t - (int64)uc->h) +
		(MAX_NUM_CACHED_PACKETS+1)
	) % (MAX_NUM_CACHED_PACKETS+1);
}
// if not full yet,
// advances tail with data. 
void udp_cache_produce(
		struct udp_cache* uc,
		struct data_len data
) {
	// recall that the ring buffer's max size is always N-1.
	if (udp_cache_size(uc) < MAX_NUM_CACHED_PACKETS)
	{
		uc->packets[uc->t] = data;
		uc->t = UCACHE_INC(uc->t);
	}
}

void
netinit(void)
{
  initlock(&netlock, "netlock");
  // Need to set all cache to null to indicate that all bindings are available.
  memset(
      &udp_port_bindings[0],
      '\0', sizeof udp_port_bindings
  );
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.

  int port;
  argint(0, &port);

  if (port < 0 || port > 65535)
    return -1;

  acquire(&netlock);

  // If the port is already bound to,
  // then we can already use it.
  if (udp_port_bindings[port].cache)
    return 0;

  // Not bound to. Take it.
  udp_port_bindings[port].cache = (struct udp_cache*)kalloc();
  struct udp_cache* cache = udp_port_bindings[port].cache;
  if (!cache)
    panic("sys_bind no mem");
  // Initialize cache
  cache->h = cache->t = 0;

  release(&netlock);
  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //

  int port;
  argint(0, &port);

  if (port < 0 || port > 65535)
    return -1;

  acquire(&netlock);
  if (!udp_port_bindings[port].cache) // not bound
    goto err_with_lock;

  struct udp_cache* cache = udp_port_bindings[port].cache;
  for (uint32 i = 0; i < udp_cache_size(cache); ++i)
  {
		udp_cache_consume(cache);
  }
  kfree((void*)cache);
  cache = 0;

  release(&netlock);
  return 0;

err_with_lock:
  release(&netlock);
  return -1;
}

// Me: this signature is misleading. The actual signature is:
// ./user/user.h:int recv(uint16, uint32*, uint16*, char *, uint32);
//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //
  uint16 dport;
  uint64 psrc;
  uint64 psport;
  uint64 buf;
  uint32 maxlen;

  argint(0, (int*)&dport);
  argaddr(1, &psrc);
  argaddr(2, &psport);
  argaddr(3, &buf);
  argint(4, (int*)&maxlen);

  int num_bytes_copied = 0;
  acquire(&netlock);
  struct port_binding* binding = &udp_port_bindings[dport];
  struct udp_cache* cache = binding->cache;
  if (!cache) // not bound
    goto err_with_lock;
  while (cache->h == cache->t) // no packets
    sleep(binding, &netlock);

	// data starts at the IP header.
	// ETH header was stripped off when it was stored.
  char* pkt = cache->packets[cache->h].data;
  struct ip* ip = (struct ip*)(pkt);
  struct udp* udp = (struct udp*)(pkt + sizeof(struct ip));
	char* payload = ((char*)udp + sizeof(struct udp));
	num_bytes_copied = cache->packets[cache->h].len -
		(sizeof(struct ip) + sizeof(struct udp));
	num_bytes_copied = num_bytes_copied > maxlen ? maxlen : num_bytes_copied;

  uint32 src = (int)bswapl(ip->ip_src);
	uint16 sport = (short)bswaps(udp->sport);

  struct proc *p = myproc();
	copyout(p->pagetable, buf, payload, num_bytes_copied);
	copyout(p->pagetable, psrc, (char*)&src, sizeof src);
	copyout(p->pagetable, psport, (char*)&sport, sizeof sport);
	
	// Do not forget to clean up and advance head
	udp_cache_consume(cache);
  
  release(&netlock);
  return num_bytes_copied;

err_with_lock:
  release(&netlock);
  return -1;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

/**
 * My own function. Called by ip_rx.
 * buf is offset by ip_rx to start at the ip header
 * and len is also adjusted.
 */
void
udp_rx(struct ip* buf, int len)
{
	struct udp* udp = (struct udp*)(
			(char*)buf + sizeof(struct ip)
	);
	uint16 dport = bswaps(udp->dport);
	struct port_binding* binding = &udp_port_bindings[dport];

	if (!binding->cache) // not bound
		goto discard;

	struct udp_cache* cache = binding->cache;
	if (udp_cache_size(cache) == MAX_NUM_CACHED_PACKETS)
		goto discard; // full

	struct data_len dat;
	dat.data = (char*)buf; dat.len = len;
	udp_cache_produce(cache, dat);

	// don't forget to wake up all processes waiting!
	wakeup(binding);
	return;
	
discard:
	kfree( ((char*)buf) - sizeof(struct eth) );
	return;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //

  // layout: 
  // ETH header
  // IP header
  // UDP header

  struct ip* ip = (struct ip*)(buf + sizeof(struct eth));
  // RFC 790 p.6 gives assigned Internet Protocol numbers.
  // Note that only byte order matters.
  // All single byte integers are the same.
  switch (ip->ip_p)
  {
  case IPPROTO_UDP:
    udp_rx(
        ip,
        len - sizeof(struct eth)
    );
    break;
  default:
    kfree(buf);
  }
  
  return;
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
