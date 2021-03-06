
/***************************************************************************
 * ncrack_sip.cc -- ncrack module for the SIP  protocol                    *
 *                                                                         *
 ***********************IMPORTANT NMAP LICENSE TERMS************************
 *                                                                         *
 * The Nmap Security Scanner is (C) 1996-2011 Insecure.Com LLC. Nmap is    *
 * also a registered trademark of Insecure.Com LLC.  This program is free  *
 * software; you may redistribute and/or modify it under the terms of the  *
 * GNU General Public License as published by the Free Software            *
 * Foundation; Version 2 with the clarifications and exceptions described  *
 * below.  This guarantees your right to use, modify, and redistribute     *
 * this software under certain conditions.  If you wish to embed Nmap      *
 * technology into proprietary software, we sell alternative licenses      *
 * (contact sales@insecure.com).  Dozens of software vendors already       *
 * license Nmap technology such as host discovery, port scanning, OS       *
 * detection, and version detection.                                       *
 *                                                                         *
 * Note that the GPL places important restrictions on "derived works", yet *
 * it does not provide a detailed definition of that term.  To avoid       *
 * misunderstandings, we consider an application to constitute a           *
 * "derivative work" for the purpose of this license if it does any of the *
 * following:                                                              *
 * o Integrates source code from Nmap                                      *
 * o Reads or includes Nmap copyrighted data files, such as                *
 *   nmap-os-db or nmap-service-probes.                                    *
 * o Executes Nmap and parses the results (as opposed to typical shell or  *
 *   execution-menu apps, which simply display raw Nmap output and so are  *
 *   not derivative works.)                                                *
 * o Integrates/includes/aggregates Nmap into a proprietary executable     *
 *   installer, such as those produced by InstallShield.                   *
 * o Links to a library or executes a program that does any of the above   *
 *                                                                         *
 * The term "Nmap" should be taken to also include any portions or derived *
 * works of Nmap.  This list is not exclusive, but is meant to clarify our *
 * interpretation of derived works with some common examples.  Our         *
 * interpretation applies only to Nmap--we don't speak for other people's  *
 * GPL works.                                                              *
 *                                                                         *
 * If you have any questions about the GPL licensing restrictions on using *
 * Nmap in non-GPL works, we would be happy to help.  As mentioned above,  *
 * we also offer alternative license to integrate Nmap into proprietary    *
 * applications and appliances.  These contracts have been sold to dozens  *
 * of software vendors, and generally include a perpetual license as well  *
 * as providing for priority support and updates as well as helping to     *
 * fund the continued development of Nmap technology.  Please email        *
 * sales@insecure.com for further information.                             *
 *                                                                         *
 * As a special exception to the GPL terms, Insecure.Com LLC grants        *
 * permission to link the code of this program with any version of the     *
 * OpenSSL library which is distributed under a license identical to that  *
 * listed in the included docs/licenses/OpenSSL.txt file, and distribute   *
 * linked combinations including the two. You must obey the GNU GPL in all *
 * respects for all of the code used other than OpenSSL.  If you modify    *
 * this file, you may extend this exception to your version of the file,   *
 * but you are not obligated to do so.                                     *
 *                                                                         *
 * If you received these files with a written license agreement or         *
 * contract stating terms other than the terms above, then that            *
 * alternative license agreement takes precedence over these comments.     *
 *                                                                         *
 * Source is provided to this software because we believe users have a     *
 * right to know exactly what a program is going to do before they run it. *
 * This also allows you to audit the software for security holes (none     *
 * have been found so far).                                                *
 *                                                                         *
 * Source code also allows you to port Nmap to new platforms, fix bugs,    *
 * and add new features.  You are highly encouraged to send your changes   *
 * to nmap-dev@insecure.org for possible incorporation into the main       *
 * distribution.  By sending these changes to Fyodor or one of the         *
 * Insecure.Org development mailing lists, it is assumed that you are      *
 * offering the Nmap Project (Insecure.Com LLC) the unlimited,             *
 * non-exclusive right to reuse, modify, and relicense the code.  Nmap     *
 * will always be available Open Source, but this is important because the *
 * inability to relicense code has caused devastating problems for other   *
 * Free Software projects (such as KDE and NASM).  We also occasionally    *
 * relicense the code to third parties as discussed above.  If you wish to *
 * specify special license conditions of your contributions, just say so   *
 * when you send them.                                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       *
 * General Public License v2.0 for more details at                         *
 * http://www.gnu.org/licenses/gpl-2.0.html , or in the COPYING file       *
 * included with Nmap.                                                     *
 *                                                                         *
 ***************************************************************************/


#include "ncrack.h"
#include "nsock.h"
#include "NcrackOps.h"
#include "Service.h"
#include "modules.h"
#include "http.h"
#ifndef WIN32
  #include <ifaddrs.h>
  #include <net/if.h>
#else
  #include <Iptypes.h>
#endif

using namespace std;

extern NcrackOps o;

extern void ncrack_read_handler(nsock_pool nsp, nsock_event nse, void *mydata);
extern void ncrack_write_handler(nsock_pool nsp, nsock_event nse, void *mydata);
extern void ncrack_module_end(nsock_pool nsp, void *mydata);

static int sip_loop_read(nsock_pool nsp, Connection *con);
static char *get_ip_address(void);


typedef struct sip_info {
  int cseq;
  u16 localport;
  char *local_ip; 
} sip_info;


enum states { SIP_INIT, SIP_STATUS, SIP_AUTH };

#define SIP_TIMEOUT 10000


void
ncrack_sip(nsock_pool nsp, Connection *con)
{
  nsock_iod nsi = con->niod;
  Service *serv = con->service;
  sip_info *info = NULL;
  struct sockaddr_storage local;
  struct sockaddr_in *local4 = NULL; 
  struct sockaddr_in6 *local6 = NULL; 
  int family = 0;
  local4 = (struct sockaddr_in *)&local;
  local6 = (struct sockaddr_in6 *)&local;
  char localport_s[6];
  char cseq_s[6];

  struct http_header *h;
  char *header;
  struct http_challenge challenge;
  char *response_hdr;

  char *la;


  if (con->misc_info) {
    info = (sip_info *) con->misc_info;
  } else {
    con->misc_info = (sip_info *) safe_zalloc(sizeof(sip_info));
    info = (sip_info *) con->misc_info;
    info->cseq = 1;
  }


  switch (con->state)
  { 

    case SIP_INIT:

      nsock_iod_get_communication_info(nsi, NULL, &family, (struct sockaddr*)&local,
                                       NULL, sizeof(struct sockaddr_storage));
      if (family == AF_INET6) {
        info->localport = ntohs(local6->sin6_port);
      } else {
        info->localport = ntohs(local4->sin_port);
      }

      snprintf(localport_s, sizeof(localport_s), "%d", info->localport);
      printf("local port %s \n", localport_s);

      snprintf(cseq_s, sizeof(cseq_s), "%d", info->cseq);


      if (con->outbuf)
        delete con->outbuf;
      con->outbuf = new Buf();

      info->local_ip = get_ip_address();

      con->outbuf->snprintf(strlen(serv->target->NameIP()) + strlen(info->local_ip) + strlen(localport_s) + strlen(con->user) \
           + strlen(serv->target->NameIP()) + strlen(con->user) + strlen(serv->target->NameIP()) + strlen(serv->target->NameIP()) \
           + strlen(cseq_s) + 
           strlen("REGISTER sip: SIP/2.0\r\nVia: SIP/2.0/TCP :\r\nFrom: <sip:@>\r\nTo: <sip:@>\r\n" "Call-ID: 1234@\r\nCSeq:  REGISTER\r\nContent-Length: 0\r\n\r\n"),
           "REGISTER sip:%s SIP/2.0\r\n"
           "Via: SIP/2.0/TCP %s:%d\r\n"
           "From: <sip:%s@%s>\r\n"
           "To: <sip:%s@%s>\r\n" "Call-ID: 1234@%s\r\n" \
           "CSeq: %i REGISTER\r\n" "Content-Length: 0\r\n\r\n", serv->target->NameIP(), info->local_ip, info->localport, con->user, 
           serv->target->NameIP(), con->user, serv->target->NameIP(), serv->target->NameIP(), info->cseq);

#if 0
      con->outbuf->snprintf(strlen("officesip.local") + strlen(info->local_ip) + strlen(localport_s) + strlen(con->user) \
           + strlen("officesip.local") + strlen(con->user) + strlen(serv->target->NameIP()) + strlen(serv->target->NameIP()) \
           + strlen(cseq_s) + 
           strlen("REGISTER sip: SIP/2.0\r\nVia: SIP/2.0/TCP :\r\nFrom: <sip:@>\r\nTo: <sip:@>\r\n" "Call-ID: 1234@\r\nCSeq:  REGISTER\r\nContent-Length: 0\r\n\r\n"),
       "REGISTER sip:%s SIP/2.0\r\n"
       "Via: SIP/2.0/TCP %s:%d\r\n"
       "From: <sip:%s@%s>\r\n"
       "To: <sip:%s@%s>\r\n" "Call-ID: 1234@%s\r\n" \
       "CSeq: %i REGISTER\r\n" "Content-Length: 0\r\n\r\n", "officesip.local", info->local_ip, info->localport, con->user, 
       "officesip.local", con->user, serv->target->NameIP(), serv->target->NameIP(), info->cseq);
#endif

      info->cseq++;

      con->state = SIP_STATUS;

      nsock_write(nsp, nsi, ncrack_write_handler, SIP_TIMEOUT, con,
        (const char *)con->outbuf->get_dataptr(), con->outbuf->get_len());

      break;

    case SIP_STATUS:

      if (sip_loop_read(nsp, con) < 0)
        break;

#if 0
      printf("---------------memprint-------------\n");
      memprint((const char *)con->inbuf->get_dataptr(), con->inbuf->get_len());
      printf("---------------memprint-------------\n");
#endif

      if (con->outbuf)
        delete con->outbuf;
      con->outbuf = new Buf();

      /* If domain is invalid, then we should stop trying to crack this
       * service.
       */
      if (memsearch((const char *)con->inbuf->get_dataptr(),
            "Domain invalid or not specified", con->inbuf->get_len())) {
        serv->end.orly = true;
        serv->end.reason = Strndup("Domain invalid or not specified.",
            strlen("Domain invalid or not specified."));
        return ncrack_module_end(nsp, con);
      }


      if (http_read_header((char *)con->inbuf->get_dataptr(), con->inbuf->get_len(),
          &header) < 0) {
          //printf("Error reading response header.\n");
          return ncrack_module_end(nsp, con);
      }

      if (http_parse_header(&h, header) != 0) {
          //printf("Error parsing response header.\n");
          return ncrack_module_end(nsp, con);
      }
      free(header);
      header = NULL;

      if (http_header_get_challenge(h, &challenge) == NULL) {
          //printf("Error getting Authenticate challenge.\n");
          http_header_free(h);
          return ncrack_module_end(nsp, con);
      }
      http_header_free(h);

      response_hdr = http_digest_proxy_authorization(&challenge, 
          con->user, con->pass, "REGISTER", "sip:officesip.local");

      //printf("resp hdr: %s\n", response_hdr);
      //printf("length: %d \n", strlen(response_hdr)); 

      snprintf(localport_s, sizeof(localport_s), "%d", info->localport);
      snprintf(cseq_s, sizeof(cseq_s), "%d", info->cseq);

      con->outbuf->snprintf(strlen(serv->target->NameIP()) + strlen(info->local_ip) + strlen(localport_s) + strlen(con->user) \
          + strlen(serv->target->NameIP()) + strlen(con->user) + strlen(serv->target->NameIP()) + strlen(serv->target->NameIP()) \
          + strlen(cseq_s) + strlen(response_hdr) +
          strlen("REGISTER sip: SIP/2.0\r\nVia: SIP/2.0/TCP :\r\nFrom: <sip:@>\r\nTo: <sip:@>\r\n" "Call-ID: 1234@\r\nCSeq:  REGISTER\r\n\Authorization:\r\nContent-Length: 0\r\n\r\n"),
      "REGISTER sip:%s SIP/2.0\r\n"
      "Via: SIP/2.0/TCP %s:%d\r\n"
      "From: <sip:%s@%s>\r\n"
      "To: <sip:%s@%s>\r\n" "Call-ID: 1234@%s\r\n" \
      "CSeq: %u REGISTER\r\n" "Authorization:%s\r\n" "Content-Length: 0\r\n\r\n", serv->target->NameIP(), info->local_ip, info->localport, con->user, 
      serv->target->NameIP(), con->user, serv->target->NameIP(), serv->target->NameIP(), info->cseq, response_hdr);

      info->cseq++;
 
#if 0
      printf("outbuf: \n");
      la = (char *)con->outbuf->get_dataptr();
      for (int i = 0; i < con->outbuf->get_len(); i++)
        printf("%c", *(la + i));
      printf("------\n");
#endif

      delete con->inbuf;
      con->inbuf = NULL;

      nsock_write(nsp, nsi, ncrack_write_handler, SIP_TIMEOUT, con,
        (const char *)con->outbuf->get_dataptr(), con->outbuf->get_len());

      con->state = SIP_AUTH;

      break;

    case SIP_AUTH:


      if (sip_loop_read(nsp, con) < 0)
        break;

#if 0
      printf("----------AUTH memprint-------------\n");
      memprint((const char *)con->inbuf->get_dataptr(), con->inbuf->get_len());
      printf("----------AUTH memprint-------------\n");
#endif

      if (memsearch((const char *)con->inbuf->get_dataptr(),
            "200 OK", con->inbuf->get_len())) {
        con->auth_success = true;
      }

      ncrack_module_end(nsp, con);
      break;

  }


}



static int
sip_loop_read(nsock_pool nsp, Connection *con)
{

  if (con->inbuf == NULL) {
    nsock_read(nsp, con->niod, ncrack_read_handler, SIP_TIMEOUT, con);
    return -1;
  }

  if (!memsearch((const char *)con->inbuf->get_dataptr(), "\r\n\r\n",
        con->inbuf->get_len())) {
    nsock_read(nsp, con->niod, ncrack_read_handler, SIP_TIMEOUT, con);
    return -1;
  }

  return 0;
}



char *get_ip_address(void) {

  char *addr = NULL;
#ifndef WIN32
  struct ifaddrs *ifaddr, *ifa;
  int family, s, n;
  char host[NI_MAXHOST];
  int addrlen;

  if (getifaddrs(&ifaddr) == -1)
    return NULL;

  for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
    if (ifa->ifa_addr == NULL)
      continue;

    family = ifa->ifa_addr->sa_family;

    if(ifa->ifa_flags & IFF_LOOPBACK)
      continue;

    /* For an AF_INET* interface address, display the address */
    if (family == AF_INET || family == AF_INET6) {
      s = getnameinfo(ifa->ifa_addr,
                      (family == AF_INET) ? sizeof(struct sockaddr_in) :
                      sizeof(struct sockaddr_in6),
                      host, NI_MAXHOST,
                      NULL, 0, NI_NUMERICHOST);
      if (s != 0) {
        printf("getnameinfo() failed: %s\n", gai_strerror(s));
        return NULL;
      }
      if (family == AF_INET) {
        addr = (char *)malloc(strlen(host) + 1);
        memset(addr, 0, strlen(host) + 1);
        memcpy(addr, host, strlen(host));
        addrlen = strlen(host);
        break;
      }
      printf("address: <%s>\n", host);

    }
  }

  if (ifa == NULL) {
    freeifaddrs(ifaddr);
    return NULL;
  }

  //printf("final addr: %s\n", addr);
  //printf("final addrlen: %d\n", addrlen);
  freeifaddrs(ifaddr);

#else

  IP_ADAPTER_INFO  *pAdapterInfo;
  ULONG            ulOutBufLen;
  DWORD            dwRetVal;
  PIP_ADAPTER_INFO pAdapter = pAdapterInfo;

  while (pAdapter) {
	  printf("Adapter Name: %s\n", pAdapter->AdapterName);
	  printf("Adapter Desc: %s\n", pAdapter->Description);
	  printf("\tAdapter Addr: \t");
	  for (UINT i = 0; i < pAdapter->AddressLength; i++) {
		  if (i == (pAdapter->AddressLength - 1))
			  printf("%.2X\n", (int)pAdapter->Address[i]);
		  else
			  printf("%.2X-", (int)pAdapter->Address[i]);
	  }
	  printf("IP Address: %s\n", pAdapter->IpAddressList.IpAddress.String);
  }


#endif

  return addr;
}

