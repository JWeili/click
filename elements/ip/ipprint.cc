/*
 * ipprint.{cc,hh} -- element prints packet contents to system log
 * Max Poletto
 *
 * Copyright (c) 2000 Mazu Networks, Inc.
 *
 * This software is being provided by the copyright holders under the GNU
 * General Public License, either version 2 or, at your discretion, any later
 * version. For more information, see the `COPYRIGHT' file in the source
 * distribution.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "ipprint.hh"
#include "glue.hh"
#include "confparse.hh"
#include "error.hh"

#include "click_ip.h"
#include "click_tcp.h"
#include "click_udp.h"

IPPrint::IPPrint()
  : Element(1, 1)
{
  _buf = 0;
}

IPPrint::~IPPrint()
{
  delete[] _buf;
}

IPPrint *
IPPrint::clone() const
{
  return new IPPrint;
}

int
IPPrint::configure(const Vector<String> &conf, ErrorHandler* errh)
{
  _bytes = 1500;
  String contents = "no";
  if (cp_va_parse(conf, this, errh,
		  cpString, "label", &_label,
		  cpOptional,
		  cpWord, "print packet contents (no/hex/ascii)", &contents,
		  cpInteger, "number of bytes to dump", &_bytes,
		  cpEnd) < 0)
    return -1;

  contents = contents.upper();
  if (contents == "NO" || contents == "FALSE")
    _contents = 0;
  else if (contents == "YES" || contents == "TRUE" || contents == "HEX")
    _contents = 1;
  else if (contents == "ASCII")
    _contents = 2;
  else
    return errh->error("bad contents value `%s'; should be NO, HEX, or ASCII", contents.cc());
  
  delete[] _buf;
  _buf = 0;

  if (_contents) {
    _buf = new char[3*_bytes+(_bytes/4+1)+3*(_bytes/24+1)+1];
    if (!_buf)
      return errh->error("out of memory");
  }
  
  return 0;
}

void
IPPrint::uninitialize()
{
  delete[] _buf;
  _buf = 0;
}

Packet *
IPPrint::simple_action(Packet *p)
{
  String s = "";
  const click_ip *iph = p->ip_header();

  if (!iph) {
    s = "(Not an IP packet)";
    return p;
  }

  IPAddress src(iph->ip_src.s_addr);
  IPAddress dst(iph->ip_dst.s_addr);
  //  unsigned char tos = iph->ip_tos;
  
  switch (iph->ip_p) {
  case IP_PROTO_TCP: {
    const click_tcp *tcph =
      reinterpret_cast<const click_tcp *>(p->transport_header());
    unsigned short srcp = ntohs(tcph->th_sport);
    unsigned short dstp = ntohs(tcph->th_dport);
    unsigned seq = ntohl(tcph->th_seq);
    unsigned ack = ntohl(tcph->th_ack);
    unsigned win = ntohs(tcph->th_win);
    int ackp = tcph->th_flags & TH_ACK;
    String flag = "";
    if (tcph->th_flags & TH_FIN)
      flag = "F";
    else if (tcph->th_flags & TH_SYN)
      flag = "S";
    else if (tcph->th_flags & TH_RST)
      flag = "R";
    if (tcph->th_flags & TH_PUSH)
      flag += "P";
    if (flag.length() == 0) flag = ".";
    s = src.s() + ":" + String((int)srcp) + " > "
      + dst.s() + ":" + String((int)dstp) + " : "
      + flag + " " + String(seq);
    if (ackp)
      s += " ack " + String(ack);
    s += " win " + String(win);
    s += " len " + String(ntohs(iph->ip_len)-iph->ip_hl*4-tcph->th_off*4);
    break;
  }
  case IP_PROTO_UDP: {
    const click_udp *udph =
      reinterpret_cast<const click_udp *>(p->transport_header());
    unsigned short srcp = ntohs(udph->uh_sport);
    unsigned short dstp = ntohs(udph->uh_dport);
    unsigned len = ntohs(udph->uh_ulen);
    s = src.s() + ":" + String((int)srcp) + " > "
      + dst.s() + ":" + String((int)dstp) 
      + " : UDP; len " + String(len);
    break;
  }
  case IP_PROTO_ICMP: {
    s = src.s() + " > " + dst.s() + " : ICMP";
    break;
  }
  default: {
    s = src.s() + " > " + dst.s() + " : unknown protocol";
    break;
  }
  }

  click_chatter("%s: %s", _label.cc(), s.cc());

  const unsigned char *data = p->data();
  if (_contents == 1) {
    int pos = 0;  
    for (unsigned i = 0; i < _bytes && i < p->length(); i++) {
      sprintf(_buf+pos, "%02x", data[i] & 0xff);
      pos += 2;
      if ((i % 24) == 23) {
	_buf[pos++] = '\n'; _buf[pos++] = ' ';	_buf[pos++] = ' ';
      } else if ((i % 4) == 3)
	_buf[pos++] = ' ';
    }
    _buf[pos++] = '\0';
#ifdef __KERNEL__
    printk("  %s\n", _buf);
#else
    fprintf(stderr, "  %s\n", _buf);
#endif
  } else if (_contents == 2) {
    int pos = 0;  
    for (unsigned i = 0; i < _bytes && i < p->length(); i++) {
      if (data[i] < 32 || data[i] > 126)
	_buf[pos++] = '.';
      else
	_buf[pos++] = data[i];
      if ((i % 48) == 47) {
	_buf[pos++] = '\n'; _buf[pos++] = ' ';	_buf[pos++] = ' ';
      } else if ((i % 8) == 7)
	_buf[pos++] = ' ';
    }
    _buf[pos++] = '\0';
#ifdef __KERNEL__
    printk("  %s\n", _buf);
#else
    fprintf(stderr, "  %s\n", _buf);
#endif
  }

  return p;
}

EXPORT_ELEMENT(IPPrint)
