/*
 * Copyright (c) 2020 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ue.h"
#include "gtpv2c_set_ie.h"
#include "../cp_dp_api/cp_dp_api.h"

struct parse_release_access_bearer_request_t {
	ue_context *context;
};
extern struct response_info resp_t;

/**
 * parses gtpv2c message and populates parse_release_access_bearer_request_t
 *   structure
 * @param gtpv2c_rx
 *   buffer containing received release access bearer request message
 * @param release_access_bearer_request
 *   structure to contain parsed information from message
 * @return
 *   \- 0 if successful
 *   \- > 0 if error occurs during packet filter parsing corresponds to 3gpp
 *   specified cause error value
 *   \- < 0 for all other errors
 */
static int
parse_release_access_bearer_request(gtpv2c_header *gtpv2c_rx,
	struct parse_release_access_bearer_request_t
	*release_access_bearer_request)
{

	uint32_t teid = ntohl(gtpv2c_rx->teid_u.has_teid.teid);
	int ret = rte_hash_lookup_data(ue_context_by_fteid_hash,
	    (const void *) &teid,
	    (void **) &release_access_bearer_request->context);

	if (ret < 0 || !release_access_bearer_request->context)
		return GTPV2C_CAUSE_CONTEXT_NOT_FOUND;
	return 0;
}

/**
 * from parameters, populates gtpv2c message 'release access bearer
 * response' and populates required information elements as defined by
 * clause 7.2.22 3gpp 29.274
 * @param gtpv2c_tx
 *   transmission buffer to contain 'release access bearer request' message
 * @param sequence
 *   sequence number as described by clause 7.6 3gpp 29.274
 * @param context
 *   UE Context data structure pertaining to the bearer to be modified
 */
void
set_release_access_bearer_response(gtpv2c_header *gtpv2c_tx,
		uint32_t sequence, ue_context *context)
{
	set_gtpv2c_teid_header(gtpv2c_tx, GTP_RELEASE_ACCESS_BEARERS_RSP,
	    htonl(context->s11_mme_gtpc_teid), sequence);

	set_cause_accepted_ie(gtpv2c_tx, IE_INSTANCE_ZERO);

}


int
process_release_access_bearer_request(gtpv2c_header *gtpv2c_rx,
		gtpv2c_header *gtpv2c_tx)
{
	int i;
	struct dp_id dp_id = { .id = DPN_ID };
	struct parse_release_access_bearer_request_t
		release_access_bearer_request = { 0 };
	RTE_SET_USED(gtpv2c_tx);

	int ret = parse_release_access_bearer_request(gtpv2c_rx,
			&release_access_bearer_request);
	if (ret) {
		/* VCCCCB-50: GTPV2C_CAUSE_CONTEXT_NOT_FOUND not send after CP restart */
		set_gtpv2c_teid_header(gtpv2c_tx, GTP_RELEASE_ACCESS_BEARERS_RSP,
		    0, gtpv2c_rx->teid_u.has_teid.seq);
		set_cause_ie(gtpv2c_tx, GTPV2C_CAUSE_CONTEXT_NOT_FOUND, IE_INSTANCE_ZERO);
		return ret;
	}

	for (i = 0; i < MAX_BEARERS; ++i) {
		if (release_access_bearer_request.context->eps_bearers[i]
				== NULL)
			continue;

		eps_bearer *bearer = release_access_bearer_request.
				context->eps_bearers[i];

		bearer->s1u_enb_gtpu_teid = 0;

		/* VCCCCB-47 ModifyBearerResponse sent during ReleaseAccessBearers Procedure
		 * Set ReleaseAccessBearers response
		 */
		resp_t.msg_type = GTP_RELEASE_ACCESS_BEARERS_REQ;
		resp_t.cause_val = GTPV2C_CAUSE_REQUEST_ACCEPTED;
		resp_t.gtpv2c_tx_t = *gtpv2c_tx;
		resp_t.gtpv2c_tx_t.teid_u.has_teid.seq = gtpv2c_rx->teid_u.has_teid.seq;
		resp_t.context_t = *release_access_bearer_request.context;
		resp_t.bearer_t = *bearer;

		/* using the s1u_sgw_gtpu_teid as unique identifier to
		 * the session */
		struct session_info session;
		memset(&session, 0, sizeof(session));

		session.ue_addr.iptype = IPTYPE_IPV4;
		session.ue_addr.u.ipv4_addr = ntohl(bearer->pdn->ipv4.s_addr);
		session.ul_s1_info.sgw_teid =
		    ntohl(bearer->s1u_sgw_gtpu_teid);
		session.ul_s1_info.sgw_addr.iptype = IPTYPE_IPV4;
		session.ul_s1_info.sgw_addr.u.ipv4_addr =
		    ntohl(bearer->s1u_sgw_gtpu_ipv4.s_addr);
		session.ul_s1_info.enb_addr.iptype = IPTYPE_IPV4;
		session.ul_s1_info.enb_addr.u.ipv4_addr =
		    ntohl(bearer->s1u_enb_gtpu_ipv4.s_addr);
		session.dl_s1_info.enb_teid =
		    ntohl(bearer->s1u_enb_gtpu_teid);
		session.dl_s1_info.enb_addr.iptype = IPTYPE_IPV4;
		session.dl_s1_info.enb_addr.u.ipv4_addr =
		    ntohl(bearer->s1u_enb_gtpu_ipv4.s_addr);
		session.dl_s1_info.sgw_addr.iptype = IPTYPE_IPV4;
		session.dl_s1_info.sgw_addr.u.ipv4_addr =
		    ntohl(bearer->s1u_sgw_gtpu_ipv4.s_addr);
		session.bearer_id = bearer->eps_bearer_id;
		session.ul_apn_mtr_idx = ulambr_idx;
		session.dl_apn_mtr_idx = dlambr_idx;
		session.num_ul_pcc_rules = 1;
		session.ul_pcc_rule_id[0] = FIRST_FILTER_ID;
		session.num_dl_pcc_rules = 1;
		session.dl_pcc_rule_id[0] = FIRST_FILTER_ID;

		session.sess_id = SESS_ID(
			release_access_bearer_request.context->
			s11_sgw_gtpc_teid,
			bearer->eps_bearer_id);

		if (session_modify(dp_id, &session) < 0)
			rte_exit(EXIT_FAILURE,
				"Bearer Session modify fail !!!");
		return 0;
	}
	return 0;
}
