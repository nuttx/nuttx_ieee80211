/****************************************************************************
 * net/ieee80211/ieee80211_ioctl.c
 * IEEE 802.11 ioctl support
 *
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <net/if.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>

#ifdef CONFIG_NET_ETHERNET
#  include <netinet/in.h>
#  include <nuttx/net/uip/uip.h>
#endif

#include <nuttx/tree.h>
#include <nuttx/net/uip/uip-arch.h>
#include <nuttx/net/ieee80211/ieee80211_var.h>
#include <nuttx/net/ieee80211/ieee80211_crypto.h>
#include <nuttx/net/ieee80211/ieee80211_ioctl.h>

void     ieee80211_node2req(struct ieee80211_s *,
        const struct ieee80211_node *, struct ieee80211_nodereq *);
void     ieee80211_req2node(struct ieee80211_s *,
        const struct ieee80211_nodereq *, struct ieee80211_node *);

void ieee80211_node2req(struct ieee80211_s *ic, const struct ieee80211_node *ni,
    struct ieee80211_nodereq *nr)
{
    /* Node address and name information */
    IEEE80211_ADDR_COPY(nr->nr_macaddr, ni->ni_macaddr);
    IEEE80211_ADDR_COPY(nr->nr_bssid, ni->ni_bssid);
    nr->nr_nwid_len = ni->ni_esslen;
    bcopy(ni->ni_essid, nr->nr_nwid, IEEE80211_NWID_LEN);

    /* Channel and rates */
    nr->nr_channel = ieee80211_chan2ieee(ic, ni->ni_chan);
    nr->nr_chan_flags = ni->ni_chan->ic_flags;
    nr->nr_nrates = ni->ni_rates.rs_nrates;
    bcopy(ni->ni_rates.rs_rates, nr->nr_rates, IEEE80211_RATE_MAXSIZE);

    /* Node status information */
    nr->nr_rssi = (*ic->ic_node_getrssi)(ic, ni);
    nr->nr_max_rssi = ic->ic_max_rssi;
    bcopy(ni->ni_tstamp, nr->nr_tstamp, sizeof(nr->nr_tstamp));
    nr->nr_intval = ni->ni_intval;
    nr->nr_capinfo = ni->ni_capinfo;
    nr->nr_erp = ni->ni_erp;
    nr->nr_pwrsave = ni->ni_pwrsave;
    nr->nr_associd = ni->ni_associd;
    nr->nr_txseq = ni->ni_txseq;
    nr->nr_rxseq = ni->ni_rxseq;
    nr->nr_fails = ni->ni_fails;
    nr->nr_inact = ni->ni_inact;
    nr->nr_txrate = ni->ni_txrate;
    nr->nr_state = ni->ni_state;
    /* XXX RSN */

    /* Node flags */
    nr->nr_flags = 0;
    if (bcmp(nr->nr_macaddr, nr->nr_bssid, IEEE80211_ADDR_LEN) == 0)
        nr->nr_flags |= IEEE80211_NODEREQ_AP;
    if (ni == ic->ic_bss)
        nr->nr_flags |= IEEE80211_NODEREQ_AP_BSS;
}

void ieee80211_req2node(struct ieee80211_s *ic, const struct ieee80211_nodereq *nr,
    struct ieee80211_node *ni)
{
    /* Node address and name information */
    IEEE80211_ADDR_COPY(ni->ni_macaddr, nr->nr_macaddr);
    IEEE80211_ADDR_COPY(ni->ni_bssid, nr->nr_bssid);
    ni->ni_esslen = nr->nr_nwid_len;
    bcopy(nr->nr_nwid, ni->ni_essid, IEEE80211_NWID_LEN);

    /* Rates */
    ni->ni_rates.rs_nrates = nr->nr_nrates;
    bcopy(nr->nr_rates, ni->ni_rates.rs_rates, IEEE80211_RATE_MAXSIZE);

    /* Node information */
    ni->ni_intval = nr->nr_intval;
    ni->ni_capinfo = nr->nr_capinfo;
    ni->ni_erp = nr->nr_erp;
    ni->ni_pwrsave = nr->nr_pwrsave;
    ni->ni_associd = nr->nr_associd;
    ni->ni_txseq = nr->nr_txseq;
    ni->ni_rxseq = nr->nr_rxseq;
    ni->ni_fails = nr->nr_fails;
    ni->ni_inact = nr->nr_inact;
    ni->ni_txrate = nr->nr_txrate;
    ni->ni_state = nr->nr_state;
}

static int ieee80211_ioctl_setnwkeys(struct ieee80211_s *ic,
    const struct ieee80211_nwkey *nwkey)
{
    struct ieee80211_key *k;
    int error, i;

    if (!(ic->ic_caps & IEEE80211_C_WEP))
        return -ENODEV;

    if (nwkey->i_wepon == IEEE80211_NWKEY_OPEN) {
        if (!(ic->ic_flags & IEEE80211_F_WEPON))
            return 0;
        ic->ic_flags &= ~IEEE80211_F_WEPON;
        return -ENETRESET;
    }
    if (nwkey->i_defkid < 1 || nwkey->i_defkid > IEEE80211_WEP_NKID)
        return -EINVAL;

    for (i = 0; i < IEEE80211_WEP_NKID; i++) {
        if (nwkey->i_key[i].i_keylen == 0 ||
            nwkey->i_key[i].i_keydat == NULL)
            continue;    /* entry not set */
        if (nwkey->i_key[i].i_keylen > IEEE80211_KEYBUF_SIZE)
            return -EINVAL;

        /* map wep key to ieee80211_key */
        k = &ic->ic_nw_keys[i];
        if (k->k_cipher != IEEE80211_CIPHER_NONE)
            (*ic->ic_delete_key)(ic, NULL, k);
        memset(k, 0, sizeof(*k));
        if (nwkey->i_key[i].i_keylen <= 5)
            k->k_cipher = IEEE80211_CIPHER_WEP40;
        else
            k->k_cipher = IEEE80211_CIPHER_WEP104;
        k->k_len = ieee80211_cipher_keylen(k->k_cipher);
        k->k_flags = IEEE80211_KEY_GROUP | IEEE80211_KEY_TX;
        error = copyin(nwkey->i_key[i].i_keydat, k->k_key, k->k_len);
        if (error != 0)
            return error;
        if ((error = (*ic->ic_set_key)(ic, NULL, k)) != 0)
            return error;
    }

    ic->ic_def_txkey = nwkey->i_defkid - 1;
    ic->ic_flags |= IEEE80211_F_WEPON;

    return -ENETRESET;
}

static int ieee80211_ioctl_getnwkeys(struct ieee80211_s *ic, struct ieee80211_nwkey *nwkey)
{
    struct ieee80211_key *k;
    int error, i;

    if (ic->ic_flags & IEEE80211_F_WEPON)
      {
        nwkey->i_wepon = IEEE80211_NWKEY_WEP;
      }
    else
      {
        nwkey->i_wepon = IEEE80211_NWKEY_OPEN;
      }

    nwkey->i_defkid = ic->ic_wep_txkey + 1;

    for (i = 0; i < IEEE80211_WEP_NKID; i++)
      {
        if (nwkey->i_key[i].i_keydat == NULL)
          {
            continue;
          }

        k = &ic->ic_nw_keys[i];
        if (k->k_cipher != IEEE80211_CIPHER_WEP40 &&
            k->k_cipher != IEEE80211_CIPHER_WEP104)
          {
            nwkey->i_key[i].i_keylen = 0;
          }
        else
          {
            nwkey->i_key[i].i_keylen = k->k_len;
          }

        error = copyout(k->k_key, nwkey->i_key[i].i_keydat, nwkey->i_key[i].i_keylen);
        if (error != 0)
          {
            return error;
          }
    }
    return 0;
}

static int ieee80211_ioctl_setwpaparms(struct ieee80211_s *ic,
    const struct ieee80211_wpaparams *wpa)
{
    if (!(ic->ic_caps & IEEE80211_C_RSN))
        return -ENODEV;

    if (!wpa->i_enabled) {
        if (!(ic->ic_flags & IEEE80211_F_RSNON))
            return 0;
        ic->ic_flags &= ~IEEE80211_F_RSNON;
        return -ENETRESET;
    }

    ic->ic_rsnprotos = 0;
    if (wpa->i_protos & IEEE80211_WPA_PROTO_WPA1)
        ic->ic_rsnprotos |= IEEE80211_PROTO_WPA;
    if (wpa->i_protos & IEEE80211_WPA_PROTO_WPA2)
        ic->ic_rsnprotos |= IEEE80211_PROTO_RSN;
    if (ic->ic_rsnprotos == 0)    /* set to default (WPA+RSN) */
        ic->ic_rsnprotos = IEEE80211_PROTO_WPA | IEEE80211_PROTO_RSN;

    ic->ic_rsnakms = 0;
    if (wpa->i_akms & IEEE80211_WPA_AKM_PSK)
        ic->ic_rsnakms |= IEEE80211_AKM_PSK;
    if (wpa->i_akms & IEEE80211_WPA_AKM_SHA256_PSK)
        ic->ic_rsnakms |= IEEE80211_AKM_SHA256_PSK;
    if (wpa->i_akms & IEEE80211_WPA_AKM_8021X)
        ic->ic_rsnakms |= IEEE80211_AKM_8021X;
    if (wpa->i_akms & IEEE80211_WPA_AKM_SHA256_8021X)
        ic->ic_rsnakms |= IEEE80211_AKM_SHA256_8021X;
    if (ic->ic_rsnakms == 0)    /* set to default (PSK) */
        ic->ic_rsnakms = IEEE80211_AKM_PSK;

    if (wpa->i_groupcipher == IEEE80211_WPA_CIPHER_WEP40)
        ic->ic_rsngroupcipher = IEEE80211_CIPHER_WEP40;
    else if (wpa->i_groupcipher == IEEE80211_WPA_CIPHER_TKIP)
        ic->ic_rsngroupcipher = IEEE80211_CIPHER_TKIP;
    else if (wpa->i_groupcipher == IEEE80211_WPA_CIPHER_CCMP)
        ic->ic_rsngroupcipher = IEEE80211_CIPHER_CCMP;
    else if (wpa->i_groupcipher == IEEE80211_WPA_CIPHER_WEP104)
        ic->ic_rsngroupcipher = IEEE80211_CIPHER_WEP104;
    else  {    /* set to default */
        if (ic->ic_rsnprotos & IEEE80211_PROTO_WPA)
            ic->ic_rsngroupcipher = IEEE80211_CIPHER_TKIP;
        else
            ic->ic_rsngroupcipher = IEEE80211_CIPHER_CCMP;
    }

    ic->ic_rsnciphers = 0;
    if (wpa->i_ciphers & IEEE80211_WPA_CIPHER_TKIP)
        ic->ic_rsnciphers |= IEEE80211_CIPHER_TKIP;
    if (wpa->i_ciphers & IEEE80211_WPA_CIPHER_CCMP)
        ic->ic_rsnciphers |= IEEE80211_CIPHER_CCMP;
    if (wpa->i_ciphers & IEEE80211_WPA_CIPHER_USEGROUP)
        ic->ic_rsnciphers = IEEE80211_CIPHER_USEGROUP;
    if (ic->ic_rsnciphers == 0)    /* set to default (TKIP+CCMP) */
        ic->ic_rsnciphers = IEEE80211_CIPHER_TKIP |
            IEEE80211_CIPHER_CCMP;

    ic->ic_flags |= IEEE80211_F_RSNON;

    return -ENETRESET;
}

static int ieee80211_ioctl_getwpaparms(struct ieee80211_s *ic,
    struct ieee80211_wpaparams *wpa)
{
    wpa->i_enabled = (ic->ic_flags & IEEE80211_F_RSNON) ? 1 : 0;

    wpa->i_protos = 0;
    if (ic->ic_rsnprotos & IEEE80211_PROTO_WPA)
        wpa->i_protos |= IEEE80211_WPA_PROTO_WPA1;
    if (ic->ic_rsnprotos & IEEE80211_PROTO_RSN)
        wpa->i_protos |= IEEE80211_WPA_PROTO_WPA2;

    wpa->i_akms = 0;
    if (ic->ic_rsnakms & IEEE80211_AKM_PSK)
        wpa->i_akms |= IEEE80211_WPA_AKM_PSK;
    if (ic->ic_rsnakms & IEEE80211_AKM_SHA256_PSK)
        wpa->i_akms |= IEEE80211_WPA_AKM_SHA256_PSK;
    if (ic->ic_rsnakms & IEEE80211_AKM_8021X)
        wpa->i_akms |= IEEE80211_WPA_AKM_8021X;
    if (ic->ic_rsnakms & IEEE80211_AKM_SHA256_8021X)
        wpa->i_akms |= IEEE80211_WPA_AKM_SHA256_8021X;

    if (ic->ic_rsngroupcipher == IEEE80211_CIPHER_WEP40)
        wpa->i_groupcipher = IEEE80211_WPA_CIPHER_WEP40;
    else if (ic->ic_rsngroupcipher == IEEE80211_CIPHER_TKIP)
        wpa->i_groupcipher = IEEE80211_WPA_CIPHER_TKIP;
    else if (ic->ic_rsngroupcipher == IEEE80211_CIPHER_CCMP)
        wpa->i_groupcipher = IEEE80211_WPA_CIPHER_CCMP;
    else if (ic->ic_rsngroupcipher == IEEE80211_CIPHER_WEP104)
        wpa->i_groupcipher = IEEE80211_WPA_CIPHER_WEP104;
    else
        wpa->i_groupcipher = IEEE80211_WPA_CIPHER_NONE;

    wpa->i_ciphers = 0;
    if (ic->ic_rsnciphers & IEEE80211_CIPHER_TKIP)
        wpa->i_ciphers |= IEEE80211_WPA_CIPHER_TKIP;
    if (ic->ic_rsnciphers & IEEE80211_CIPHER_CCMP)
        wpa->i_ciphers |= IEEE80211_WPA_CIPHER_CCMP;
    if (ic->ic_rsnciphers & IEEE80211_CIPHER_USEGROUP)
        wpa->i_ciphers = IEEE80211_WPA_CIPHER_USEGROUP;

    return 0;
}

static int ieee80211_ioctl_setphymode(struct ieee80211_s *ic, enum ieee80211_phymode mode)
{
    /* Sane check, we need to receive a valid mode */
    if (mode < IEEE80211_MODE_AUTO || mode >= IEEE80211_MODE_MAX)
        return -EINVAL;

    /* Verify if chip supports requested mode */
    if ((ic->ic_modecaps & (1<<mode)) == 0)
        return -EINVAL;

    /*
     * Handle phy mode change.
     */
    if (ic->ic_curmode != mode) {     /* change phy mode */
        error = ieee80211_setmode(ic, mode);
        if (error != 0)
            return error;

        /* Reset chip to accept this new phy mode */
        ieee80211_reset_erp(ic);
        return -ENETRESET;
    }

    return 0;
}

static int ieee80211_ioctl_getphymode(struct ieee80211_s *ic)
{

}

static int ieee80211_ioctl_setopmode(struct ieee80211_s *ic, enum ieee80211_opmode opmode)
{
    /*
     * Handle operating mode change.
     */
    if (ic->ic_opmode != opmode) {
        ic->ic_opmode = opmode;
#ifdef CONFIG_IEEE80211_AP
        switch (opmode) {
            case IEEE80211_M_AHDEMO:
            case IEEE80211_M_HOSTAP:
            case IEEE80211_M_STA:
            case IEEE80211_M_MONITOR:
                ic->ic_flags &= ~IEEE80211_F_IBSSON;
                break;
            case IEEE80211_M_IBSS:
                ic->ic_flags |= IEEE80211_F_IBSSON;
                break;
        }
#endif

        /* Reset chip to accept this new operating mode */
        ieee80211_reset_erp(ic);
        return -ENETRESET;
    }

    return 0;
}

static int ieee80211_ioctl_getopmode(struct ieee80211_s *ic)
{

}

static int ieee80211_ioctl_setfixedrate(struct ieee80211_s *ic, int rate)
{
    /* ic_fix_rate is the index to rate, it isn't the rate value */
    int idxrate = 0;

    /* Check if we received a valid rate */
    if (rate <= 0)
        return -EINVAL;          

    /* Check if this rate is valid for the current phy mode */
    if (!isvalidrate(ic->ic_curmode, rate))
        return -EINVAL;

    /* Find the index for this rate in this current mode */
    idxrate = ieee80211_findrate(ic, ic->ic_curmode, rate);

    /*
     * Committed to changes, install the rate setting.
     */
    if (ic->ic_fixed_rate != idxrate) {
        ic->ic_fixed_rate = idxrate;          /* set fixed tx rate */

        /* Reset chip to accept this new fixed rate */
        ieee80211_reset_erp(ic);
        return -ENETRESET;
    }

    return 0;
}

static int ieee80211_ioctl_getfixedrate(struct ieee80211_s *ic)
{

}

bool isvalidrate(enum ieee80211_phymode mode, int rate)
{
  int i, ratemode;
  static const int rates[] =
  {
    {   2 | IEEE80211_MODE_11B << 16},
    {   4 | IEEE80211_MODE_11B << 16},
    {  11 | IEEE80211_MODE_11B << 16},
    {  22 | IEEE80211_MODE_11B << 16},
    {  44 | IEEE80211_MODE_11B << 16},
    {  12 | IEEE80211_MODE_11A << 16},
    {  18 | IEEE80211_MODE_11A << 16},
    {  24 | IEEE80211_MODE_11A << 16},
    {  36 | IEEE80211_MODE_11A << 16},
    {  48 | IEEE80211_MODE_11A << 16},
    {  72 | IEEE80211_MODE_11A << 16},
    {  96 | IEEE80211_MODE_11A << 16},
    { 108 | IEEE80211_MODE_11A << 16},
    {   2 | IEEE80211_MODE_11G << 16},
    {   4 | IEEE80211_MODE_11G << 16},
    {  11 | IEEE80211_MODE_11G << 16},
    {  22 | IEEE80211_MODE_11G << 16},
    {  12 | IEEE80211_MODE_11G << 16},
    {  18 | IEEE80211_MODE_11G << 16},
    {  24 | IEEE80211_MODE_11G << 16},
    {  36 | IEEE80211_MODE_11G << 16},
    {  48 | IEEE80211_MODE_11G << 16},
    {  72 | IEEE80211_MODE_11G << 16},
    {  96 | IEEE80211_MODE_11G << 16},
    { 108 | IEEE80211_MODE_11G << 16},
  };

  ratemode = rate & (mode << 16);

  for (i = 0; i < N(rates); i++)
    {
      if (rates[i] == ratemode)
        {
          return true;
        }
    }

  return false;
}

int ieee80211_ioctl(struct ieee80211_s *ic, unsigned long cmd, void *data)
{
    struct ifreq *ifr = (struct ifreq *)data;
    int i, error = 0;
    struct ieee80211_nwid nwid;
    struct ieee80211_wpapsk *psk;
    struct ieee80211_wmmparams *wmm;
    struct ieee80211_keyavail *ka;
    struct ieee80211_keyrun *kr;
    struct ieee80211_power *power;
    struct ieee80211_bssid *bssid;
    struct ieee80211chanreq *chanreq;
    struct ieee80211_channel *chan;
    struct ieee80211_txpower *txpower;
    static const uint8_t empty_macaddr[IEEE80211_ADDR_LEN] =
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    struct ieee80211_nodereq *nr, nrbuf;
    struct ieee80211_nodereq_all *na;
    struct ieee80211_node *ni;
    uint32_t flags;
    int ndx;
    int bit;

    switch (cmd) {
    case SIOCS80211NWID:
        if ((error = copyin(ifr->ifr_data, &nwid, sizeof(nwid))) != 0)
            break;
        if (nwid.i_len > IEEE80211_NWID_LEN) {
            error = -EINVAL;
            break;
        }
        memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
        ic->ic_des_esslen = nwid.i_len;
        memcpy(ic->ic_des_essid, nwid.i_nwid, nwid.i_len);
        error = -ENETRESET;
        break;
    case SIOCG80211NWID:
        memset(&nwid, 0, sizeof(nwid));
        switch (ic->ic_state) {
        case IEEE80211_S_INIT:
        case IEEE80211_S_SCAN:
            nwid.i_len = ic->ic_des_esslen;
            memcpy(nwid.i_nwid, ic->ic_des_essid, nwid.i_len);
            break;
        default:
            nwid.i_len = ic->ic_bss->ni_esslen;
            memcpy(nwid.i_nwid, ic->ic_bss->ni_essid, nwid.i_len);
            break;
        }
        error = copyout(&nwid, ifr->ifr_data, sizeof(nwid));
        break;
    case SIOCS80211NWKEY:
        error = ieee80211_ioctl_setnwkeys(ic, (void *)data);
        break;
    case SIOCG80211NWKEY:
        error = ieee80211_ioctl_getnwkeys(ic, (void *)data);
        break;
    case SIOCS80211WMMPARMS:
        if (!(ic->ic_flags & IEEE80211_C_QOS)) {
            error = -ENODEV;
            break;
        }
        wmm = (struct ieee80211_wmmparams *)data;
        if (wmm->i_enabled)
            ic->ic_flags |= IEEE80211_F_QOS;
        else
            ic->ic_flags &= ~IEEE80211_F_QOS;
        error = -ENETRESET;
        break;
    case SIOCG80211WMMPARMS:
        wmm = (struct ieee80211_wmmparams *)data;
        wmm->i_enabled = (ic->ic_flags & IEEE80211_F_QOS) ? 1 : 0;
        break;
    case SIOCS80211WPAPARMS:
        error = ieee80211_ioctl_setwpaparms(ic, (void *)data);
        break;
    case SIOCG80211WPAPARMS:
        error = ieee80211_ioctl_getwpaparms(ic, (void *)data);
        break;
    case SIOCS80211WPAPSK:
        psk = (struct ieee80211_wpapsk *)data;
        if (psk->i_enabled) {
            ic->ic_flags |= IEEE80211_F_PSK;
            memcpy(ic->ic_psk, psk->i_psk, sizeof(ic->ic_psk));
        } else {
            ic->ic_flags &= ~IEEE80211_F_PSK;
            memset(ic->ic_psk, 0, sizeof(ic->ic_psk));
        }
        error = -ENETRESET;
        break;
    case SIOCG80211WPAPSK:
        psk = (struct ieee80211_wpapsk *)data;
        if (ic->ic_flags & IEEE80211_F_PSK) {
            psk->i_enabled = 1;
            memcpy(psk->i_psk, ic->ic_psk, sizeof(psk->i_psk));
        } else
            psk->i_enabled = 0;
        break;
    case SIOCS80211KEYAVAIL:
        ka = (struct ieee80211_keyavail *)data;
        (void)ieee80211_pmksa_add(ic, IEEE80211_AKM_8021X,
            ka->i_macaddr, ka->i_key, ka->i_lifetime);
        break;
    case SIOCS80211KEYRUN:
        kr = (struct ieee80211_keyrun *)data;
        error = ieee80211_keyrun(ic, kr->i_macaddr);
        break;
    case SIOCS80211POWER:
        power = (struct ieee80211_power *)data;
        ic->ic_lintval = power->i_maxsleep;
        if (power->i_enabled != 0) {
            if ((ic->ic_caps & IEEE80211_C_PMGT) == 0)
                error = -EINVAL;
            else if ((ic->ic_flags & IEEE80211_F_PMGTON) == 0) {
                ic->ic_flags |= IEEE80211_F_PMGTON;
                error = -ENETRESET;
            }
        } else {
            if (ic->ic_flags & IEEE80211_F_PMGTON) {
                ic->ic_flags &= ~IEEE80211_F_PMGTON;
                error = -ENETRESET;
            }
        }
        break;
    case SIOCG80211POWER:
        power = (struct ieee80211_power *)data;
        power->i_enabled = (ic->ic_flags & IEEE80211_F_PMGTON) ? 1 : 0;
        power->i_maxsleep = ic->ic_lintval;
        break;
    case SIOCS80211BSSID:
        bssid = (struct ieee80211_bssid *)data;
        if (IEEE80211_ADDR_EQ(bssid->i_bssid, empty_macaddr))
            ic->ic_flags &= ~IEEE80211_F_DESBSSID;
        else {
            ic->ic_flags |= IEEE80211_F_DESBSSID;
            IEEE80211_ADDR_COPY(ic->ic_des_bssid, bssid->i_bssid);
        }
#ifdef CONFIG_IEEE80211_AP
        if (ic->ic_opmode == IEEE80211_M_HOSTAP)
            break;
#endif
        switch (ic->ic_state) {
        case IEEE80211_S_INIT:
        case IEEE80211_S_SCAN:
            error = -ENETRESET;
            break;
        default:
            if ((ic->ic_flags & IEEE80211_F_DESBSSID) &&
                !IEEE80211_ADDR_EQ(ic->ic_des_bssid,
                ic->ic_bss->ni_bssid))
                error = -ENETRESET;
            break;
        }
        break;
    case SIOCG80211BSSID:
        bssid = (struct ieee80211_bssid *)data;
        switch (ic->ic_state) {
        case IEEE80211_S_INIT:
        case IEEE80211_S_SCAN:
#ifdef CONFIG_IEEE80211_AP
            if (ic->ic_opmode == IEEE80211_M_HOSTAP)
                IEEE80211_ADDR_COPY(bssid->i_bssid,
                    ic->ic_myaddr);
            else
#endif
            if (ic->ic_flags & IEEE80211_F_DESBSSID)
                IEEE80211_ADDR_COPY(bssid->i_bssid,
                    ic->ic_des_bssid);
            else
                memset(bssid->i_bssid, 0, IEEE80211_ADDR_LEN);
            break;
        default:
            IEEE80211_ADDR_COPY(bssid->i_bssid,
                ic->ic_bss->ni_bssid);
            break;
        }
        break;

    case SIOCS80211CHANNEL:
        chanreq = (struct ieee80211chanreq *)data;

        ndx = ((int)chanreq->i_channel >> 3);
        bit = ((int)chanreq->i_channel & 7);

        if (chanreq->i_channel == IEEE80211_CHAN_ANY)
          {
            ic->ic_des_chan = IEEE80211_CHAN_ANYC;
          }
        else if (chanreq->i_channel > IEEE80211_CHAN_MAX ||
                 (ic->ic_chan_active[ndx] & (1 << bit)) == 0)
          {
            error = -EINVAL;
            break;
          }
        else
          {
            ic->ic_ibss_chan = ic->ic_des_chan =  &ic->ic_channels[chanreq->i_channel];
          }

        switch (ic->ic_state) {
        case IEEE80211_S_INIT:
        case IEEE80211_S_SCAN:
            error = -ENETRESET;
            break;
        default:
            if (ic->ic_opmode == IEEE80211_M_STA) {
                if (ic->ic_des_chan != IEEE80211_CHAN_ANYC &&
                    ic->ic_bss->ni_chan != ic->ic_des_chan)
                    error = -ENETRESET;
            } else {
                if (ic->ic_bss->ni_chan != ic->ic_ibss_chan)
                    error = -ENETRESET;
            }
            break;
        }
        break;
    case SIOCG80211CHANNEL:
        chanreq = (struct ieee80211chanreq *)data;
        switch (ic->ic_state) {
        case IEEE80211_S_INIT:
        case IEEE80211_S_SCAN:
            if (ic->ic_opmode == IEEE80211_M_STA)
                chan = ic->ic_des_chan;
            else
                chan = ic->ic_ibss_chan;
            break;
        default:
            chan = ic->ic_bss->ni_chan;
            break;
        }
        chanreq->i_channel = ieee80211_chan2ieee(ic, chan);
        break;
    case SIOCG80211ALLCHANS:
        error = copyout(ic->ic_channels,
            ((struct ieee80211_chanreq_all *)data)->i_chans,
            sizeof(ic->ic_channels));
        break;

    case SIOCS80211TXPOWER:
        txpower = (struct ieee80211_txpower *)data;
        if ((ic->ic_caps & IEEE80211_C_TXPMGT) == 0) {
            error = -EINVAL;
            break;
        }
        if (!(IEEE80211_TXPOWER_MIN <= txpower->i_val &&
            txpower->i_val <= IEEE80211_TXPOWER_MAX)) {
            error = -EINVAL;
            break;
        }
        ic->ic_txpower = txpower->i_val;
        error = -ENETRESET;
        break;
    case SIOCG80211TXPOWER:
        txpower = (struct ieee80211_txpower *)data;
        if ((ic->ic_caps & IEEE80211_C_TXPMGT) == 0)
            error = -EINVAL;
        else
            txpower->i_val = ic->ic_txpower;
        break;
    case SIOCS80211SCAN:
      {
       FAR struct uip_driver_s *dev;

#ifdef CONFIG_IEEE80211_AP
        if (ic->ic_opmode == IEEE80211_M_HOSTAP)
          {
            break;
          }
#endif

        /* Get the driver structure and test the interface flags to make
         * sure that the interface is up.
         */

        dev = netdev_findbyaddr(ic->ic_ifname);
        if (!dev)
          {
            error = -ENODEV;
            break;
          }
        else if ((dev->d_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING))
          {
            error = -ENETDOWN;
            break;
          }

        if ((ic->ic_scan_lock & IEEE80211_SCAN_REQUEST) == 0)
          {
            if (ic->ic_scan_lock & IEEE80211_SCAN_LOCKED)
              {
                ic->ic_scan_lock |= IEEE80211_SCAN_RESUME;
              }

            ic->ic_scan_lock |= IEEE80211_SCAN_REQUEST;
            if (ic->ic_state != IEEE80211_S_SCAN)
              {
                ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
              }
          }

        /* Let the userspace process wait for completion */

        error = sleep(IEEE80211_SCAN_TIMEOUT);
      }
      break;
    case SIOCG80211NODE:
        nr = (struct ieee80211_nodereq *)data;
        ni = ieee80211_find_node(ic, nr->nr_macaddr);
        if (ni == NULL)
          {
            error = -ENOENT;
            break;
          }

        ieee80211_node2req(ic, ni, nr);
        break;
    case SIOCS80211NODE:
#ifdef CONFIG_IEEE80211_AP
        if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
            error = -EINVAL;
            break;
        }
#endif
        nr = (struct ieee80211_nodereq *)data;

        ni = ieee80211_find_node(ic, nr->nr_macaddr);
        if (ni == NULL)
            ni = ieee80211_alloc_node(ic, nr->nr_macaddr);
        if (ni == NULL) {
            error = -ENOENT;
            break;
        }

        if (nr->nr_flags & IEEE80211_NODEREQ_COPY)
            ieee80211_req2node(ic, nr, ni);
        break;
#ifdef CONFIG_IEEE80211_AP
    case SIOCS80211DELNODE:
        nr = (struct ieee80211_nodereq *)data;
        ni = ieee80211_find_node(ic, nr->nr_macaddr);
        if (ni == NULL)
            error = -ENOENT;
        else if (ni == ic->ic_bss)
            error = -EPERM;
        else {
            if (ni->ni_state == IEEE80211_STA_COLLECT)
                break;

            /* Disassociate station. */
            if (ni->ni_state == IEEE80211_STA_ASSOC)
                IEEE80211_SEND_MGMT(ic, ni,
                    IEEE80211_FC0_SUBTYPE_DISASSOC,
                    IEEE80211_REASON_ASSOC_LEAVE);

            /* Deauth station. */
            if (ni->ni_state >= IEEE80211_STA_AUTH)
                IEEE80211_SEND_MGMT(ic, ni,
                    IEEE80211_FC0_SUBTYPE_DEAUTH,
                    IEEE80211_REASON_AUTH_LEAVE);

            ieee80211_node_leave(ic, ni);
        }
        break;
#endif
    case SIOCG80211ALLNODES:
        na = (struct ieee80211_nodereq_all *)data;
        na->na_nodes = i = 0;
        ni = RB_MIN(ieee80211_tree, &ic->ic_tree);
        while (ni && na->na_size >=
            i + sizeof(struct ieee80211_nodereq)) {
            ieee80211_node2req(ic, ni, &nrbuf);
            error = copyout(&nrbuf, (void *)na->na_node + i,
                sizeof(struct ieee80211_nodereq));
            if (error < 0)
                break;
            i += sizeof(struct ieee80211_nodereq);
            na->na_nodes++;
            ni = RB_NEXT(ieee80211_tree, &ic->ic_tree, ni);
        }
        break;
    case SIOCG80211FLAGS:
        flags = ic->ic_flags;
#ifdef CONFIG_IEEE80211_AP
        if (ic->ic_opmode != IEEE80211_M_HOSTAP)
#endif
            flags &= ~IEEE80211_F_HOSTAPMASK;
        ifr->ifr_flags = flags >> IEEE80211_F_USERSHIFT;
        break;
    case SIOCS80211FLAGS:
        flags = (uint32_t)ifr->ifr_flags << IEEE80211_F_USERSHIFT;
        if (
#ifdef CONFIG_IEEE80211_AP
            ic->ic_opmode != IEEE80211_M_HOSTAP &&
#endif
            (flags & IEEE80211_F_HOSTAPMASK)) {
            error = -EINVAL;
            break;
        }
        ic->ic_flags = (ic->ic_flags & ~IEEE80211_F_USERMASK) | flags;
        error = -ENETRESET;
        break;

    case SIOCG80211ZSTATS: /* No statistics */
    case SIOCG80211STATS:  /* No statistics */
    case SIOCSIFMTU:       /* MTU is fixed by the NuttX configuration */
    default:
      error = -ENOTTY;
    }

    return error;
}
