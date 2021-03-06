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

#include <stdlib.h>
#include <stddef.h>
#include <time.h>

#include "dns_cache.h"
#include "gatekeeper_multi_curl.h"
#include "gatekeeper_single_curl.h"
#include "gatekeeper_cache.h"
#include "gatekeeper_data.h"
#include "gatekeeper_msg.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "wc_telemetry.h"
#include "gatekeeper.h"
#include "ds_tree.h"
#include "memutil.h"
#include "schema.h"
#include "const.h"
#include "log.h"

#define GK_NOT_RATED 100
#define GK_UNRATED_TTL (60*60*24)

static ovsdb_table_t table_SSL;

static struct fsm_gk_mgr cache_mgr =
{
    .initialized = false,
};


/**
 * @brief returns the plugin's session manager
 *
 * @return the plugin's session manager
 */
struct fsm_gk_mgr *
gatekeeper_get_mgr(void)
{
    return &cache_mgr;
}


/* Local log interval */
#define GATEKEEPER_LOG_PERIODIC 120

static void
gatekeeper_log_health_stats(struct fsm_gk_session *fsm_gk_session,
                            struct wc_health_stats *hs)
{
    struct fsm_session *session;

    session = fsm_gk_session->session;
    LOGI("%s: session %s activity report", __func__,
         session->name);

    LOGI("%s : session %s reported lookup failures: %d", __func__,
         session->name,
         fsm_gk_session->reported_lookup_failures);

    if (fsm_gk_session->remote_lookup_retries != 0)
    {
        LOGI("gatekeeper: reported lookup retries: %d",
             fsm_gk_session->remote_lookup_retries);
    }
    LOGI("%s: total lookups: %u", __func__, hs->total_lookups);
    LOGI("%s: total cache hits: %u", __func__, hs->cache_hits);
    LOGI("%s: total remote lookups: %u", __func__, hs->remote_lookups);
    LOGI("%s: cloud uncategorized responses: %u", __func__,
         hs->uncategorized);
    LOGI("%s: cache entries: [%u/%u]", __func__,
         hs->cached_entries, hs->cache_size);
    LOGI("%s: min lookup latency in ms: %u", __func__,
         hs->min_latency);
    LOGI("%s: max lookup latency in ms: %u", __func__,
         hs->max_latency);
    LOGI("%s: avg lookup latency in ms: %u", __func__,
         hs->avg_latency);
}

/**
 * @brief computes health stats for gatekeeper
 *
 * @param fsm_gk_session pointer to gatekeeper session
 * @param hs pointer to health stats
 */
void
gatekeeper_report_compute_health_stats(struct fsm_gk_session *fsm_gk_session,
                                       struct wc_health_stats *hs)
{
    struct fsm_url_stats *stats;
    uint32_t count;
    uint32_t dns_cache_hits;

    stats = &fsm_gk_session->health_stats;
    dns_cache_hits = dns_cache_get_hit_count(IP2ACTION_GK_SVC);

    count = (uint32_t)(stats->cloud_lookups + stats->cache_hits + dns_cache_hits);
    hs->total_lookups = count;

    /* Compute cache hits */
    count = (uint32_t)(stats->cache_hits) + dns_cache_hits;
    hs->cache_hits = count;

    /* Compute remote_lookups */
    count = (uint32_t)(stats->cloud_lookups);
    hs->remote_lookups = count;

    /* Compute connectivity_failures */
    hs->connectivity_failures = fsm_gk_session->gk_offline.connection_failures;
    fsm_gk_session->gk_offline.connection_failures = 0;

    /* Compute service_failures */
    count = (uint32_t)(stats->categorization_failures);
    hs->service_failures = count;

    /* Compute uncategorized requests */
    count = (uint32_t)(stats->uncategorized);
    hs->uncategorized = count;

    /* Compute min latency */
    count = (uint32_t)(stats->min_lookup_latency);
    hs->min_latency = count;
    /* if lookup has not happened, set min latency to 0 */
    if (stats->min_lookup_latency == LONG_MAX) hs->min_latency = 0;

    /* Compute max latency */
    count = (uint32_t)(stats->max_lookup_latency);
    hs->max_latency = count;

    /* Compute average latency */
    if (stats->cloud_lookups != 0)
    {
        double avg;

        /* avg_lookup_latency is the summation of all latencies */
        avg = stats->avg_lookup_latency;
        avg /= stats->cloud_lookups;
        hs->avg_latency = (uint32_t)avg;
    }

    /* Compute cached entries */
    stats->cache_entries = gk_get_cache_count();
    count = (uint32_t)(stats->cache_entries);
    hs->cached_entries = count;

    /* Compute cache size */
    stats->cache_size = GK_MAX_CACHE_ENTRIES;
    count = (uint32_t)(stats->cache_size);
    hs->cache_size = count;
}

static void
gatekeeper_report_health_stats(struct fsm_gk_session *fsm_gk_session,
                               time_t now)
{
    struct fsm_url_report_stats report_stats;
    struct wc_packed_buffer *serialized;
    struct wc_observation_window ow;
    struct wc_observation_point op;
    struct wc_stats_report report;
    struct fsm_session *session;
    struct wc_health_stats hs;

    memset(&report, 0, sizeof(report));
    memset(&ow, 0, sizeof(ow));
    memset(&op, 0, sizeof(op));
    memset(&hs, 0, sizeof(hs));
    memset(&report_stats, 0, sizeof(report_stats));
    session = fsm_gk_session->session;

    /* Set observation point */
    op.location_id = session->location_id;
    op.node_id = session->node_id;

    /* set observation window */
    ow.started_at = fsm_gk_session->stat_report_ts;
    ow.ended_at = now;
    fsm_gk_session->stat_report_ts = now;

    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);

    /* Log locally */
    gatekeeper_log_health_stats(fsm_gk_session, &hs);

    /* Prepare report */
    report.provider = session->name;
    report.op = &op;
    report.ow = &ow;
    report.health_stats = &hs;

    /* Serialize report */
    serialized = wc_serialize_wc_stats_report(&report);
    if (serialized == NULL) return;

    /* Emit report */
    session->ops.send_pb_report(session, fsm_gk_session->health_stats_report_topic,
                                serialized->buf, serialized->len);

    /* Free the serialized protobuf */
    wc_free_packed_buffer(serialized);

}


/**
 * OVSDB monitor update callback for SSL table
 */
static void
callback_SSL(ovsdb_update_monitor_t *mon,
             struct schema_SSL *old_rec,
             struct schema_SSL *ssl)
{
    struct fsm_gk_mgr *mgr;
    ds_tree_t *sessions;
    struct fsm_gk_session *fsm_gk_session;
    struct gk_curl_easy_info *ecurl;

    if (!ssl->certificate_exists || !ssl->private_key_exists) return;

    LOGD("%s(): reading SSL certs on update", __func__);

    mgr = gatekeeper_get_mgr();
    if (!mgr->initialized) return;

    sessions = &mgr->fsm_sessions;
    fsm_gk_session = ds_tree_head(sessions);
    while (fsm_gk_session != NULL)
    {
        ecurl = &fsm_gk_session->ecurl;
        strncpy(ecurl->ssl_cert, ssl->certificate, sizeof(ecurl->ssl_cert));
        strncpy(ecurl->ssl_key, ssl->private_key, sizeof(ecurl->ssl_key));
        strncpy(ecurl->ca_path, ssl->ca_cert, sizeof(ecurl->ca_path));

        LOGD("%s(): ssl cert %s, priv key %s, ca_path: %s",
             __func__,
             ecurl->ssl_cert,
             ecurl->ssl_key,
             ecurl->ca_path);

        fsm_gk_session = ds_tree_next(sessions, fsm_gk_session);
    }
}

/**
 * @brief frees fsm_gk_verdict structure
 *
 * @return None
 */
void
free_gk_verdict(struct fsm_gk_verdict *gk_verdict)
{
    gk_free_packed_buffer(gk_verdict->gk_pb);
    free(gk_verdict);
}

void
gk_update_redirect_from_cache(struct fsm_policy_req *req,
                              struct gk_attr_cache_interface *entry)
{
    struct fqdn_pending_req *fqdn_req;

    fqdn_req = req->fqdn_req;

    /* update redirect entries only for attr type FQDN */
    if (entry->attribute_type != GK_CACHE_REQ_TYPE_FQDN) return;

    /* return if fqdn_redirect is not set */
    if (entry->fqdn_redirect == NULL) return;

    /* if redirect flag is not set, return
     * without updating redirect entries
     */
    if (!entry->fqdn_redirect->redirect) return;

    req->reply.redirect = entry->fqdn_redirect->redirect;
    req->reply.rd_ttl   = entry->fqdn_redirect->redirect_ttl;

    STRSCPY(fqdn_req->redirects[0], entry->fqdn_redirect->redirect_ips[0]);
    STRSCPY(fqdn_req->redirects[1], entry->fqdn_redirect->redirect_ips[1]);
}

/**
 * @brief checks if the policy is present in attribute cache.
 *
 * @param req: the request being processed
 * @return true if present false otherwise
 */
static bool
gatekeeper_check_attr_cache(struct fsm_policy_req *req, int req_type)
{
    struct gk_attr_cache_interface *entry;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    bool ret;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    url_reply = req_info->reply;

    entry = CALLOC(1, sizeof(struct gk_attr_cache_interface));
    if (entry == NULL) return false;

    entry->fqdn_redirect = CALLOC(1, sizeof(struct fqdn_redirect_s));
    if (entry->fqdn_redirect == NULL)
    {
        free(entry);
        return false;
    }

    entry->device_mac = req->device_id;
    entry->attribute_type = req_type;
    entry->attr_name = strdup(req->url);
    ret = gkc_lookup_attribute_entry(entry, true);
    LOGT("%s(): %s of type %d, is %s in cache",
         __func__,
         req->url,
         req_type,
         ret ? "found" : "not found");
    if (ret)
    {
        req->reply.action = entry->action;

        gk_update_redirect_from_cache(req, entry);

        fqdn_req->categorized = entry->categorized;
        url_reply->reply_info.gk_info.category_id = entry->category_id;
        url_reply->reply_info.gk_info.confidence_level = entry->confidence_level;
        url_reply->service_id = URL_GK_SVC;
        if (entry->gk_policy != NULL)
        {
            url_reply->reply_info.gk_info.gk_policy = strdup(entry->gk_policy);
        }
        req->fqdn_req->from_cache = true;
    }

    FREE(entry->attr_name);
    FREE(entry->gk_policy);
    FREE(entry->fqdn_redirect);
    FREE(entry);
    return ret;
}

/**
 * @brief checks if the policy is present in the IP flow cache.
 *
 * @param req: the request being processed
 * @return true if present false otherwise
 */
static bool
gatekeeper_check_ipflow_cache(struct fsm_policy_req *req)
{
    struct gkc_ip_flow_interface *flow_entry;
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_key *key;
    bool ret;

    acc = req->acc;
    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;

    flow_entry = calloc(sizeof(struct gkc_ip_flow_interface), 1);
    if (flow_entry == NULL) return false;

    flow_entry->device_mac = req->device_id;
    flow_entry->direction = acc->direction;
    flow_entry->ip_version = key->ip_version;
    flow_entry->protocol = key->ipprotocol;
    flow_entry->src_ip_addr = key->src_ip;
    flow_entry->dst_ip_addr = key->dst_ip;
    flow_entry->src_port = key->sport;
    flow_entry->dst_port = key->dport;

    ret = gkc_lookup_flow(flow_entry, true);
    if (ret == true)
    {
        /* value found in cache update reply */
        req->reply.action = flow_entry->action;
        req->fqdn_req->from_cache = true;
    }

    free(flow_entry);

    return ret;
}

/**
 * @brief checks if the policy is present in the attribute cache.
 *
 * @param req: the request being processed
 * @return true if present false otherwise
 */
bool
gk_check_policy_in_cache(struct fsm_policy_req *req)
{
    struct gk_cache_mgr *cache_mgr;
    int req_type;
    bool ret = false;

    cache_mgr = gk_cache_get_mgr();
    if (!cache_mgr->initialized) return false;

    req_type = fsm_policy_get_req_type(req);
    if (req_type >=FSM_FQDN_REQ && req_type <= FSM_APP_REQ)
    {
        LOGT("%s(): checking attribute cache", __func__);
        ret = gatekeeper_check_attr_cache(req, req_type);
    }
    else if (req_type >= FSM_IPV4_FLOW_REQ && req_type <= FSM_IPV6_FLOW_REQ)
    {
        LOGT("%s(): checking IP Flow cache", __func__);
        ret = gatekeeper_check_ipflow_cache(req);
    }

    return ret;
}

/**
 * @brief Populates redirect entries for fqdn attribute cache.
 *
 * @param req: the request being processed
 * @param entry: cache entry to be updated
 * @return true if the success false otherwise
 */
static void
gk_populate_redirect_entry(struct gk_attr_cache_interface *entry,
                           struct fsm_policy_req *req)
{
    struct fqdn_redirect_s *redirect_entry;
    struct fqdn_pending_req *fqdn_req;

    if (req->reply.redirect == false) return;

    entry->fqdn_redirect = CALLOC(1, sizeof(*redirect_entry));
    if (entry->fqdn_redirect == NULL) return;
    redirect_entry = entry->fqdn_redirect;

    fqdn_req       = req->fqdn_req;

    redirect_entry->redirect     = req->reply.redirect;
    redirect_entry->redirect_ttl = req->reply.rd_ttl;
    STRSCPY(redirect_entry->redirect_ips[0], fqdn_req->redirects[0]);
    STRSCPY(redirect_entry->redirect_ips[1], fqdn_req->redirects[1]);

    LOGT("%s(): populated redirect entries for gk cache, redirect flag %d "
         "IPv4 %s, IPv6 %s",
         __func__,
         redirect_entry->redirect,
         redirect_entry->redirect_ips[0],
         redirect_entry->redirect_ips[1]);
}

/**
 * @brief Populates the entries and adds to attribute cache.
 *
 * @param req: the request being processed
 * @return true if the success false otherwise
 */
static bool
gatekeeper_add_attr_cache(struct fsm_policy_req *req, int req_type)
{
    struct gk_attr_cache_interface *entry;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    bool ret;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    url_reply = req_info->reply;

    if (url_reply == NULL)
    {
        LOGD("%s: url_reply is NULL, not adding to attr cache", __func__);
        return false;
    }

    entry = calloc(sizeof(struct gk_attr_cache_interface), 1);
    if (entry == NULL) return false;

    entry->action = req->reply.action;
    entry->device_mac = req->device_id;
    entry->attribute_type = req_type;
    entry->cache_ttl = req->reply.cache_ttl;
    entry->categorized = fqdn_req->categorized;
    entry->attr_name = strdup(req->url);
    entry->category_id = url_reply->reply_info.gk_info.category_id;
    entry->confidence_level = url_reply->reply_info.gk_info.confidence_level;
    if (url_reply->reply_info.gk_info.gk_policy)
    {
        entry->gk_policy = url_reply->reply_info.gk_info.gk_policy;
    }

    /* check if fqdn redirect entries needs to be added. */
    if (req_type == GK_CACHE_REQ_TYPE_FQDN) gk_populate_redirect_entry(entry, req);

    ret = gkc_add_attribute_entry(entry);
    LOGT("%s(): adding %s (attr type %d) to cache %s ",
         __func__,
         req->url,
         req_type,
         (ret == true) ? "success" : "failed");

    FREE(entry->fqdn_redirect);
    FREE(entry->attr_name);
    FREE(entry);
    return ret;
}

/**
 * @brief Populates the entries and adds to IP Flow cache.
 *
 * @param req: the request being processed
 * @return true if the success false otherwise
 */
static bool
gatekeeper_add_ipflow_cache(struct fsm_policy_req *req)
{
    struct gkc_ip_flow_interface *flow_entry;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    struct net_md_stats_accumulator *acc;
    struct flow_key *fkey;
    struct net_md_flow_key *key;
    bool ret;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    url_reply = req_info->reply;

    if (url_reply == NULL)
    {
        LOGD("%s: url_reply is NULL, not adding to IP flow cache", __func__);
        return false;
    }

    acc = req->acc;
    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;

    fkey = acc->fkey;

    LOGT("%s: adding flow src: %s, dst: %s, proto: %d, sport: %d, dport: %d to cache",
         __func__,
         fkey->src_ip, fkey->dst_ip, fkey->protocol, fkey->sport, fkey->dport);

    flow_entry = calloc(sizeof(struct gkc_ip_flow_interface), 1);
    if (flow_entry == NULL) return false;

    flow_entry->device_mac = req->device_id;
    flow_entry->direction = acc->direction;
    flow_entry->ip_version = key->ip_version;
    flow_entry->protocol = key->ipprotocol;
    flow_entry->cache_ttl = req->reply.cache_ttl;
    flow_entry->action = req->reply.action;
    flow_entry->src_ip_addr = key->src_ip;
    flow_entry->dst_ip_addr = key->dst_ip;
    flow_entry->src_port = key->sport;
    flow_entry->dst_port = key->dport;
    flow_entry->category_id = url_reply->reply_info.gk_info.category_id;
    flow_entry->confidence_level = url_reply->reply_info.gk_info.confidence_level;
    if (url_reply->reply_info.gk_info.gk_policy)
    {
        flow_entry->gk_policy = url_reply->reply_info.gk_info.gk_policy;
    }

    ret = gkc_add_flow_entry(flow_entry);
    LOGT("%s(): adding cache %s ",
         __func__,
         (ret == true) ? "success" : "failed");

    free(flow_entry);

    return ret;
}

void
gk_fsm_adjust_ttl(struct fsm_policy_req *req)
{
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    uint32_t category_id;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    url_reply = req_info->reply;

    if (url_reply == NULL) return;

    category_id = url_reply->reply_info.gk_info.category_id;
    if (category_id != GK_NOT_RATED) return;

    LOGD("%s: setting cache ttl for %s to %d seconds", __func__,
         req->url, GK_UNRATED_TTL);

    req->reply.cache_ttl = GK_UNRATED_TTL;
    fqdn_req->cat_unknown_to_service = true;
}

/**
 * @brief Add entry to the cache based on request type.
 *
 * @param req: the request being processed
 * @return true if the success false otherwise
 */
bool
gk_add_policy_to_cache(struct fsm_policy_req *req)
{
    struct gk_cache_mgr *cache_mgr;
    int req_type;
    bool ret = false;

    cache_mgr = gk_cache_get_mgr();
    if (!cache_mgr->initialized) return false;

    /* Overwrite TTL to large value for local ip and set fqdn flag */
    gk_fsm_adjust_ttl(req);

    req_type = fsm_policy_get_req_type(req);
    if (req_type >=FSM_FQDN_REQ && req_type <= FSM_APP_REQ)
    {
        ret = gatekeeper_add_attr_cache(req, req_type);
    }
    else if (req_type >= FSM_IPV4_FLOW_REQ && req_type <= FSM_IPV6_FLOW_REQ)
    {
        ret = gatekeeper_add_ipflow_cache(req);
    }

    return ret;
}

/**
 * @brief compute the latency indicators for a given request
 *
 * @param gk_session the session holding the latency indicators
 * @param start the clock_t value before the categorization API call
 * @param end the clock_t value before the categorization API call
 */
static long
fsm_gk_update_latencies(struct fsm_gk_session *gk_session,
                        struct timespec *start, struct timespec *end)
{
    struct fsm_url_stats *stats;
    long nanoseconds;
    long latency;
    long seconds;

    stats = &gk_session->health_stats;

    seconds = end->tv_sec - start->tv_sec;
    nanoseconds = end->tv_nsec - start->tv_nsec;

    /* Compute latency in nanoseconds */
    latency = (seconds * 1000000000L) + nanoseconds;

    /* Translate in milliseconds */
    latency /= 1000000L;

    stats->avg_lookup_latency += latency;

    if (latency < stats->min_lookup_latency)
    {
        stats->min_lookup_latency = latency;
    }

    if (latency > stats->max_lookup_latency)
    {
        stats->max_lookup_latency = latency;
    }

    return latency;
}

/**
 * @brief performs check whether to allow or block this packet.
 *        by connecting ot the guard server.
 *
 * @param session the fsm session
 * @req: the request being processed
 * @policy: the policy being checked against
 * @return true if the success false otherwise
 */
bool
gatekeeper_get_verdict(struct fsm_session *session,
                       struct fsm_policy_req *req)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_curl_easy_info *ecurl_info;
    struct gatekeeper_offline *offline;
    struct fsm_policy_req *policy_req;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_gk_verdict *gk_verdict;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    struct fsm_url_stats *stats;
    struct timespec start;
    struct timespec end;
    long lookup_latency;
    bool ret = true;
    bool incache;
    int gk_response;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;

    url_reply = calloc(1, sizeof(struct fsm_url_reply));
    if (url_reply == NULL) return false;

    req_info->reply = url_reply;

    url_reply->service_id = URL_GK_SVC;

    fsm_gk_session = gatekeeper_lookup_session(session->service);
    if (fsm_gk_session ==  NULL) return false;

    stats = &fsm_gk_session->health_stats;
    offline = &fsm_gk_session->gk_offline;

    incache = gk_check_policy_in_cache(req);
    if (incache == true)
    {
        stats->cache_hits++;
        LOGT("%s(): %s found in cache, returning action %d, redirect flag %d from cache",
             __func__,
             req->url,
             req->reply.action,
             req->reply.redirect);
        return true;
    }

    if (offline->provider_offline)
    {
        time_t now = time(NULL);
        bool backoff;

        backoff = ((now - offline->offline_ts) < offline->check_offline);

        if (backoff) return false;
        offline->provider_offline = false;
    }

    ecurl_info = &fsm_gk_session->ecurl;
    if (!ecurl_info->server_url) return false;

    gk_verdict = calloc(1, sizeof(*gk_verdict));
    if (gk_verdict == NULL) return false;

    gk_verdict->policy_req = req;
    policy_req = gk_verdict->policy_req;
    fqdn_req = policy_req->fqdn_req;

    LOGT("%s: url:%s path:%s", __func__, ecurl_info->server_url, ecurl_info->ca_path);

    gk_verdict->gk_pb = gatekeeper_get_req(session, req);
    if (gk_verdict->gk_pb == NULL) {
        LOGD("%s() curl request serialization failed", __func__);
        ret = false;
        goto error;
    }

    fqdn_req->categorized = FSM_FQDN_CAT_SUCCESS;

#ifdef CURL_MULTI
    gk_new_conn(req->url);
#else
    memset(&start, 0, sizeof(start));
    memset(&end, 0, sizeof(end));

    clock_gettime(CLOCK_REALTIME, &start);
    gk_response = gk_send_request(session, fsm_gk_session, gk_verdict);
    if (gk_response != GK_LOOKUP_SUCCESS)
    {
        fqdn_req->categorized = FSM_FQDN_CAT_FAILED;
        /* if connection error, start the backoff timer */
        if (gk_response == GK_CONNECTION_ERROR)
        {
            offline->provider_offline = true;
            offline->offline_ts = time(NULL);
            offline->connection_failures++;
        }

        LOGD("%s() curl error not updating cache", __func__);
        ret = false;
        goto error;
    }
    clock_gettime(CLOCK_REALTIME, &end);

    /* update stats for processing the request */
    lookup_latency = fsm_gk_update_latencies(fsm_gk_session, &start, &end);
    LOGT("%s(): cloud lookup latency for '%s' is %ld ms", __func__, req->url, lookup_latency);
#endif

    gk_add_policy_to_cache(req);

error:
    LOGT("%s(): verdict for '%s' is %d", __func__, req->url, req->reply.action);
    free_gk_verdict(gk_verdict);
    return ret;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions match
 */
static int
fsm_gk_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


/**
 * @brief initializes gate keeper plugin
 *
 * Initializes the gate keeper plugin once, with the parameters
 * given within the session.
 * @param session the fsm session containing the BC service config
 */
bool
gatekeeper_init(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;

    mgr = gatekeeper_get_mgr();
    if (mgr->initialized) return true;

    memset(&mgr->req_ids, 0, sizeof(mgr->req_ids));

    mgr->initialized = true;
    return true;
}

/**
 * @brief initializes curl handler
 *
 * @param session the fsm session containing the gatekeeper service
 */
bool
gatekeeper_init_curl(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_curl_easy_info *ecurl_info;
    struct fsm_gk_mgr *gk_mgr;

    fsm_gk_session = (struct fsm_gk_session *)session->handler_ctxt;
    if (!fsm_gk_session) return false;

    gk_mgr = gatekeeper_get_mgr();
    if (!gk_mgr->initialized) return false;

    LOGN("%s(): initializing curl", __func__);

    ecurl_info = &fsm_gk_session->ecurl;


    memset(ecurl_info, 0, sizeof(struct gk_curl_easy_info));
    ecurl_info->server_url = session->ops.get_config(session, "gk_url");

#ifdef MULTI_CURL
    gk_multi_curl_init(session->loop);
#else
    gk_curl_easy_init(fsm_gk_session, session->loop);
#endif

    return true;
}

void
gatekeeper_monitor_ssl_table(void)
{
    LOGN("%s(): monitoring SSL table", __func__);
    OVSDB_TABLE_INIT_NO_KEY(SSL);
    OVSDB_TABLE_MONITOR_F(SSL, ((char*[]){"ca_cert", "certificate", "private_key", NULL}));
}

/**
 * @brief initialized gatekeeper plugin module
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
gatekeeper_module_init(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_web_cat_ops *cat_ops;
    struct fsm_gk_mgr *mgr;
    time_t now;

    if (session == NULL) return -1;

    mgr = gatekeeper_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        bool ret;

        ret = gatekeeper_init(session);
        if (!ret) return 0;

        gk_cache_init();

        ds_tree_init(&mgr->fsm_sessions, fsm_gk_session_cmp,
                     struct fsm_gk_session, session_node);
        mgr->gk_time = time(NULL);
        mgr->initialized = true;
    }

    /* Look up the fsm bc session */
    fsm_gk_session = gatekeeper_lookup_session(session);
    if (fsm_gk_session == NULL)
    {
        LOGE("%s: could not allocate gate keeper plugin", __func__);
        gk_curl_exit();
        return -1;
    }

    /* Bail if the session is already initialized */
    if (fsm_gk_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.periodic = gatekeeper_periodic;
    session->ops.update = gatekeeper_update;
    session->ops.exit = gatekeeper_exit;
    session->handler_ctxt = fsm_gk_session;

    fsm_gk_session->session = session;
    gatekeeper_update(session);

    gatekeeper_init_curl(session);

    /* Set the plugin specific ops */
    cat_ops = &session->p_ops->web_cat_ops;
    cat_ops->gatekeeper_req = gatekeeper_get_verdict;

    now = time(NULL);
    fsm_gk_session->stat_report_ts = now;

    fsm_gk_session->gk_offline.check_offline = 30;
    fsm_gk_session->gk_offline.provider_offline = false;

    /* initialize latency counter */
    fsm_gk_session->health_stats.min_lookup_latency = LONG_MAX;

    fsm_gk_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;
}

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
gatekeeper_plugin_init(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;
    int ret;

    if (session == NULL) return -1;

    mgr = gatekeeper_get_mgr();

    if (!mgr->initialized)
    {
        gatekeeper_monitor_ssl_table();
    }

    ret = gatekeeper_module_init(session);

    return ret;
}


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
gatekeeper_exit(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_gk_mgr *mgr;

    mgr = gatekeeper_get_mgr();
    if (!mgr->initialized) return;

    fsm_gk_session = (struct fsm_gk_session *)session->handler_ctxt;
    if (!fsm_gk_session) return;

    gk_curl_easy_cleanup(fsm_gk_session);
    mgr->initialized = false;

    gatekeeper_delete_session(session);

    return;
}

/**
 * @brief close curl connection if it is ideal for more then
 *        set GK_CURL_TIMEOUT value. When curl request
 *        is made, the connection_time value is udpated.
 *
 * @param ctime current time value
 */
void
gk_curl_easy_timeout(struct fsm_gk_session *fsm_gk_session, time_t ctime)
{
    struct gk_curl_easy_info *curl_info;
    double time_diff;

    curl_info = &fsm_gk_session->ecurl;

    if (curl_info->connection_active == false) return;

    time_diff = ctime - curl_info->connection_time;

    if (time_diff < GK_CURL_TIMEOUT) return;

    LOGD("%s(): closing curl connection.", __func__);
    gk_curl_easy_cleanup(fsm_gk_session);
}

/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * Sends a flow stats report.
 * @param session the fsm session
 */
void
gatekeeper_periodic(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_url_stats *stats;
    struct fsm_gk_mgr *mgr;
    double cmp_report;
    bool get_stats;
    time_t now;

    fsm_gk_session = (struct fsm_gk_session *)session->handler_ctxt;
    if (!fsm_gk_session) return;

    mgr = gatekeeper_get_mgr();
    if (!mgr->initialized) return;

    now = time(NULL);

    /* Check if the time has come to report the stats through mqtt */
    cmp_report = now - fsm_gk_session->stat_report_ts;
    get_stats = (cmp_report >= fsm_gk_session->health_stats_report_interval);

    /* No need to gather stats, bail */
    if (get_stats)
    {
        /* Report to mqtt */
        gatekeeper_report_health_stats(fsm_gk_session, now);

        /* Reset stat counters */
        stats = &fsm_gk_session->health_stats;
        memset(stats, 0, sizeof(*stats));
        stats->min_lookup_latency = LONG_MAX;
    }

    /* Proceed to other periodic tasks */
    if ((now - mgr->gk_time) < GK_PERIODIC_INTERVAL) return;
    mgr->gk_time = now;

    gk_curl_easy_timeout(fsm_gk_session, now);

    /* remove expired entries from cache */
    gkc_ttl_cleanup();
}

#define GATEKEEPER_REPORT_HEALTH_STATS_INTERVAL (60*10)

void
gatekeeper_update(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_curl_easy_info *ecurl_info;
    char *hs_report_interval;
    char *hs_report_topic;
    long interval;

    fsm_gk_session = (struct fsm_gk_session *)session->handler_ctxt;
    if (!fsm_gk_session) return;

    ecurl_info = &fsm_gk_session->ecurl;

    ecurl_info->server_url = session->ops.get_config(session, "gk_url");

    fsm_gk_session->health_stats_report_interval = (long)GATEKEEPER_REPORT_HEALTH_STATS_INTERVAL;
    hs_report_interval = session->ops.get_config(session,
                                                 "wc_health_stats_interval_secs");
    if (hs_report_interval != NULL)
    {
        interval = strtoul(hs_report_interval,  NULL, 10);
        fsm_gk_session->health_stats_report_interval = (long)interval;
    }
    hs_report_topic = session->ops.get_config(session,
                                              "wc_health_stats_topic");
    fsm_gk_session->health_stats_report_topic = hs_report_topic;
}

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_gk_session *
gatekeeper_lookup_session(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;
    struct fsm_gk_session *wc_session;
    ds_tree_t *sessions;

    mgr = gatekeeper_get_mgr();
    sessions = &mgr->fsm_sessions;

    wc_session = ds_tree_find(sessions, session);
    if (wc_session != NULL) return wc_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    wc_session = calloc(1, sizeof(*wc_session));
    if (wc_session == NULL) return NULL;

    ds_tree_insert(sessions, wc_session, session);

    return wc_session;
}


/**
 * @brief Frees a gate keeper session
 *
 * @param wc_session the gate keeper session to delete
 */
void
gatekeeper_free_session(struct fsm_gk_session *wc_session)
{
    free(wc_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the wc session to delete
 */
void
gatekeeper_delete_session(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;
    struct fsm_gk_session *wc_session;
    ds_tree_t *sessions;

    mgr = gatekeeper_get_mgr();
    sessions = &mgr->fsm_sessions;

    wc_session = ds_tree_find(sessions, session);
    if (wc_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, wc_session);
    gatekeeper_free_session(wc_session);

    return;
}


