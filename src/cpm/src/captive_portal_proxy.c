/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <errno.h>

#include "evx.h"
#include "ovsdb_utils.h"
#include "daemon.h"

#include "os.h"
#include "const.h"
#include "util.h"
#include "log.h"

#include "captive_portal.h"

#define TINYPROXY_CONF_PATH      CONFIG_CPM_TINYPROXY_ETC "/tinyproxy.conf"

#define TINYPROXY_DEBOUNCE_TIMER      1.0

static char cportal_proxy_conf_header[] =
    "#\n"
    "# Auto-generated by OpenSync\n"
    "#\n"
    "\n"
    "DisableHttpErrors Yes\n"
    "Syslog on\n"
    "Loglevel connect\n"
    "Timeout 600\n"
    "MaxClients 255\n"
    "XTinyproxy Yes\n"
    "XTinyproxy-MAC Yes\n"
    "\n";

static bool cportal_proxy_global_init = false;
static daemon_t cportal_proxy_process;
static ev_debounce cportal_proxy_debounce;

struct cportal_proxy_other_config {
    char *listenip;
    char *listenport;
    char *xtinyproxy_hdr;
    char *xtinyproxy_mac_hdr;
    char *viahdr;
    char *timeout;
    char *max_clients;
    char *loglvl;
};

static char *
cportal_proxy_get_other_config_val(struct cportal *self, char *key)
{
    ds_tree_t       *conf_pairs;
    struct str_pair *pair;

    if (!self->other_config || !key) return NULL;

    conf_pairs = self->other_config;
    pair = ds_tree_find(conf_pairs, key);
    if (!pair) return NULL;

    return pair->value;
}

static void
cportal_proxy_fill_vals(struct cportal *self, struct cportal_proxy_other_config *pconf)
{
    if (!self || !pconf) return;

    if (!self->other_config) return;

   pconf->listenip =  cportal_proxy_get_other_config_val(self, "listenip");
   pconf->listenport =  cportal_proxy_get_other_config_val(self, "listenport");
   pconf->xtinyproxy_hdr =  cportal_proxy_get_other_config_val(self, "xtinyproxyhdr");
   pconf->xtinyproxy_hdr =  cportal_proxy_get_other_config_val(self, "xtinyproxymachdr");
   pconf->viahdr =  cportal_proxy_get_other_config_val(self, "viaproxyname");
   pconf->timeout =  cportal_proxy_get_other_config_val(self, "timeout");
   pconf->max_clients =  cportal_proxy_get_other_config_val(self, "max_clients");
   pconf->loglvl =  cportal_proxy_get_other_config_val(self, "loglevel");
}

static bool
cportal_proxy_write_additional_hdrs(struct cportal *self)
{
    FILE *fconf = NULL;
    ds_tree_t       *add_hdrs;
    struct str_pair *pair = NULL;

    if (!self) return false;

    if (!self->additional_headers) return false;

    fconf = fopen(TINYPROXY_CONF_PATH, "a");
    if (!fconf) fconf = fopen(TINYPROXY_CONF_PATH, "w");
    if (!fconf)
    {
        LOG(ERR, "%s: Error creating tinyproxy config file: %s", __func__, TINYPROXY_CONF_PATH);
        return false;
    }

    add_hdrs = self->additional_headers;

    pair = ds_tree_head(add_hdrs);
    while (pair != NULL)
    {
        LOGT("%s: key:%s value:%s\n",__func__,pair->key, pair->value);
        fprintf(fconf, "AddHeader \"%s\" \"%s\"\n", pair->key, pair->value);
        pair = ds_tree_next(add_hdrs, pair);
    }


    fflush(fconf);

    if (fconf != NULL) fclose(fconf);
    return true;
}

static bool
cportal_proxy_write_other_config(struct cportal *self)
{
    FILE *fconf = NULL;
    struct cportal_proxy_other_config pconf;

    if (!self) return false;

    memset(&pconf, 0, sizeof(struct cportal_proxy_other_config));
    cportal_proxy_fill_vals(self, &pconf);

    fconf = fopen(TINYPROXY_CONF_PATH, "w");
    if (fconf == NULL)
    {
        LOG(ERR, "%s: Error creating tinyproxy config file: %s", __func__, TINYPROXY_CONF_PATH);
        return false;
    }

    fprintf(fconf, "%s\n", cportal_proxy_conf_header);

    if (pconf.listenip)
        fprintf(fconf, "Listen %s\n", pconf.listenip);
    else
        fprintf(fconf, "Listen 127.0.0.1\n");

    if (pconf.listenport)
        fprintf(fconf, "port %s\n", pconf.listenport);
    else
        fprintf(fconf, "port 8888\n");

    if (self->uam_url)
        fprintf(fconf, "upstream http %s\n", self->uam_url);

    if (pconf.xtinyproxy_hdr)
        fprintf(fconf, "XTinyproxy %s\n",pconf.xtinyproxy_hdr);

    if (pconf.xtinyproxy_mac_hdr)
        fprintf(fconf, "XTinyproxy-MAC %s\n",pconf.xtinyproxy_hdr);

    if (pconf.viahdr)
        fprintf(fconf, "ViaProxyName \"%s\"\n", pconf.viahdr);
    else
        fprintf(fconf, "DisableViaHeader Yes\n");

    if (pconf.timeout)
        fprintf(fconf, "Timeout %s\n", pconf.timeout);

    if (pconf.max_clients)
        fprintf(fconf, "MaxClients %s\n", pconf.max_clients);

    if (pconf.loglvl)
        fprintf(fconf, "LogLevel %s\n", pconf.loglvl);


    fflush(fconf);

    if (fconf != NULL) fclose(fconf);
    return true;
}

static bool
cportal_proxy_setup_nw_cfg(struct cportal *self, bool enable)
{
    char cmd_buff[512] = {0};
    char *pkt_mark = "0xa";
    char *rt_tbl_id = "100";
    char *listenip;
    char *listenport;

    if (!self) return false;

    if (self->pkt_mark) pkt_mark = self->pkt_mark;
    if (self->rt_tbl_id) rt_tbl_id = self->rt_tbl_id;
    listenip = cportal_proxy_get_other_config_val(self, "listenip");
    if (!listenip) listenip = "127.0.0.1";
    listenport = cportal_proxy_get_other_config_val(self, "listenport");
    if (!listenport) listenport = "8888";

    snprintf(cmd_buff, sizeof(cmd_buff),
             "iptables -t mangle %s PREROUTING -m mark --mark %s -p tcp --dport 80"
             " -j TPROXY --on-port %s --on-ip %s",
             enable ? "-A" : "-D",
             pkt_mark, listenport, listenip);
    if (cmd_log(cmd_buff) == -1)
    {
        LOGE("%s: Failed to execute command %s", __func__, cmd_buff);
        return false;
    }

    memset(cmd_buff, 0, sizeof(cmd_buff));
    snprintf(cmd_buff, sizeof(cmd_buff),
             "ip rule %s fwmark %s lookup %s",
             enable ? "add" : "del",
             pkt_mark, rt_tbl_id);
    if (cmd_log(cmd_buff) == -1)
    {
        LOGE("%s: Failed to execute command %s", __func__, cmd_buff);
        return false;
    }

    memset(cmd_buff, 0, sizeof(cmd_buff));
    snprintf(cmd_buff, sizeof(cmd_buff),
             "ip route %s local default dev lo table %s",
             enable ? "add" : "del",
             rt_tbl_id);
    LOGT("%s: command %s", __func__, cmd_buff);
    if (cmd_log(cmd_buff) == -1)
    {
        LOGE("%s: Failed to execute command %s", __func__, cmd_buff);
        return false;
    }

    return true;
}

static bool
cportal_proxy_write_config(struct cportal *self)
{

    if (!self) return false;

    if (mkdir(CONFIG_CPM_TINYPROXY_ETC, 0700) != 0 && errno != EEXIST)
    {
        LOG(ERR, "%s: Error creating tinyproxy config dir: %s", __func__, CONFIG_CPM_TINYPROXY_ETC);
        return false;
    }


    if (!cportal_proxy_write_other_config(self))
    {
        LOGE("%s: Failed to write proxy config.",__func__);
    }

    if (!cportal_proxy_write_additional_hdrs(self))
    {
        LOGW("%s: Failed to write additional headers config.",__func__);
    }
    return true;
}

/**
 * Global restart of the tinyproxy service
 */
static void
__cportal_proxy_restart(void)
{
    if (!cportal_proxy_global_init) return;

    LOG(INFO, "%s: daemon restart...\n",__func__);

    if (!daemon_stop(&cportal_proxy_process))
    {
        LOG(WARN, "%s: Error stopping the tinyproxy process.",__func__);
    }

    if (!daemon_start(&cportal_proxy_process))
    {
        LOG(ERR, "%s: Error starting tinyproxy.",__func__);
        goto exit;
    }

exit:
    return;
}

static void
cportal_proxy_restart_debounce(struct ev_loop *loop, ev_debounce *ev, int revent)
{
    (void)loop;
    (void)ev;
    (void)revent;

    __cportal_proxy_restart();
}

static void
cportal_proxy_restart(void)
{
    /* Do a delayed restart */
    ev_debounce_start(EV_DEFAULT, &cportal_proxy_debounce);
}

bool cportal_proxy_init(void)
{

    if (!cportal_proxy_global_init)
    {
        /*
         * Initialize the tinyproxy global instance and process
         */
        ev_debounce_init(&cportal_proxy_debounce, cportal_proxy_restart_debounce, TINYPROXY_DEBOUNCE_TIMER);

        if (!daemon_init(&cportal_proxy_process, CONFIG_CPM_TINYPROXY_PATH, 0))
        {
            LOG(ERR, "%s: Error initializing proxy.", __func__);
            return false;
        }

        daemon_arg_add(&cportal_proxy_process, "-d");                             /* Run in foreground */
        daemon_arg_add(&cportal_proxy_process, "-c", TINYPROXY_CONF_PATH);   /* Config file path */

        /* Path to the PID file */
        if (!daemon_pidfile_set(&cportal_proxy_process, "/var/run/tinyproxy.pid", false))
        {
            LOG(ERR, "%s: Error initializing proxy pid file.", __func__);
            return false;
        }

        cportal_proxy_global_init = true;
    }

    return true;
}

/**
 * Start the service.
 */
bool cportal_proxy_start(struct cportal *self)
{
    cportal_proxy_restart();
    self->enabled = true;
    return true;
}

/**
 * Stop the tinyproxy service
 */
bool cportal_proxy_stop(struct cportal *self)
{
    if (!cportal_proxy_setup_nw_cfg(self, false))
    {
        LOG(ERR, "%s: Error setting up network config.", __func__);
        return false;
    }

    ev_debounce_stop(EV_DEFAULT, &cportal_proxy_debounce);

    if (!daemon_stop(&cportal_proxy_process))
    {
        LOG(WARN, "%s: Error stopping tinyproxy server.",__func__);
        return false;
    }

    cportal_proxy_global_init = true;
    return true;
}

bool cportal_proxy_set(struct cportal *self)
{
    if (!cportal_proxy_global_init) return false;

    if (!cportal_proxy_write_config(self))
    {
        LOG(ERR, "%s: Error writing tinyproxy config file[%s].",__func__,
                  TINYPROXY_CONF_PATH);
        return false;
    }

    if (!cportal_proxy_setup_nw_cfg(self, true))
    {
        LOG(ERR, "%s: Error setting up network config.", __func__);
        return false;
    }

    return true;
}
