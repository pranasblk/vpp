/*
 *------------------------------------------------------------------
 * Copyright (c) 2017 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *------------------------------------------------------------------
 */

#include <igmp/igmp.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>

/* define message IDs */
#include <igmp/igmp_msg_enum.h>

/* define message structures */
#define vl_typedefs
#include <igmp/igmp_all_api_h.h>
#undef vl_typedefs

/* define generated endian-swappers */
#define vl_endianfun
#include <igmp/igmp_all_api_h.h>
#undef vl_endianfun

/* instantiate all the print functions we know about */
#define vl_print(handle, ...) vlib_cli_output (handle, __VA_ARGS__)
#define vl_printfun
#include <igmp/igmp_all_api_h.h>
#undef vl_printfun

/* Get the API version number */
#define vl_api_version(n,v) static u32 api_version=(v);
#include <igmp/igmp_all_api_h.h>
#undef vl_api_version

#include <vlibapi/api_helper_macros.h>

#define foreach_igmp_plugin_api_msg                      \
_(IGMP_LISTEN, igmp_listen)                              \
_(IGMP_ENABLE_DISABLE, igmp_enable_disable)              \
_(IGMP_DUMP, igmp_dump)					 \
_(IGMP_CLEAR_INTERFACE, igmp_clear_interface)		 \
_(WANT_IGMP_EVENTS, want_igmp_events)			 \

static void
vl_api_igmp_listen_t_handler (vl_api_igmp_listen_t * mp)
{
  vlib_main_t *vm = vlib_get_main ();
  vnet_main_t *vnm = vnet_get_main ();
  igmp_main_t *im = &igmp_main;
  vl_api_igmp_listen_reply_t *rmp;
  int rv = 0;
  ip46_address_t saddr, gaddr;

  if (!vnet_sw_interface_is_api_valid (vnm, ntohl (mp->sw_if_index)))
    {
      rv = VNET_API_ERROR_INVALID_SW_IF_INDEX;
      goto done;
    }

  if ((vnet_sw_interface_get_flags (vnm, ntohl (mp->sw_if_index)) &&
       VNET_SW_INTERFACE_FLAG_ADMIN_UP) == 0)
    {
      rv = VNET_API_ERROR_UNEXPECTED_INTF_STATE;
      goto done;
    }

  clib_memcpy (&saddr.ip4.as_u8, mp->saddr, sizeof (u8) * 4);
  clib_memcpy (&gaddr.ip4.as_u8, mp->gaddr, sizeof (u8) * 4);

  rv = igmp_listen (vm, mp->enable, ntohl (mp->sw_if_index), saddr, gaddr, 1);

done:;
  unix_shared_memory_queue_t *q =
    vl_api_client_index_to_input_queue (mp->client_index);
  if (!q)
    return;

  rmp = vl_msg_api_alloc (sizeof (*rmp));
  rmp->_vl_msg_id = htons ((VL_API_IGMP_LISTEN_REPLY) + im->msg_id_base);
  rmp->context = mp->context;
  rmp->retval = htonl (rv);

  vl_msg_api_send_shmem (q, (u8 *) & rmp);
}

static void
vl_api_igmp_enable_disable_t_handler (vl_api_igmp_enable_disable_t * mp)
{
  vl_api_igmp_enable_disable_reply_t *rmp;
  igmp_main_t *im = &igmp_main;
  int rv = 0;

  REPLY_MACRO (VL_API_IGMP_ENABLE_DISABLE_REPLY + im->msg_id_base);
}

static void
send_igmp_details (unix_shared_memory_queue_t * q, igmp_main_t * im,
		   igmp_config_t * config, igmp_group_t * group,
		   igmp_src_t * src, u32 context)
{
  vl_api_igmp_details_t *mp;

  mp = vl_msg_api_alloc (sizeof (*mp));
  memset (mp, 0, sizeof (*mp));

  mp->_vl_msg_id = htons (VL_API_IGMP_DETAILS + im->msg_id_base);
  mp->context = context;
  mp->sw_if_index = htonl (config->sw_if_index);
  clib_memcpy (mp->saddr, &src->addr.ip4, sizeof (u8) * 4);
  clib_memcpy (mp->gaddr, &group->addr.ip4, sizeof (u8) * 4);

  vl_msg_api_send_shmem (q, (u8 *) & mp);
}

static void
vl_api_igmp_dump_t_handler (vl_api_igmp_dump_t * mp)
{
  igmp_main_t *im = &igmp_main;
  igmp_config_t *config;
  igmp_group_t *group;
  igmp_src_t *src;

  unix_shared_memory_queue_t *q =
    vl_api_client_index_to_input_queue (mp->client_index);
  if (!q)
    return;

  if (mp->dump_all)
    {
      /* *INDENT-OFF* */
      pool_foreach (config, im->configs, (
        {
	    pool_foreach (group, config->groups, (
	      {
		pool_foreach (src, group->srcs, (
		  {
		    send_igmp_details (q, im, config, group, src, mp->context);
		  }));
	      }));
        }));
      /* *INDENT-ON* */
      return;
    }
  config = igmp_config_lookup (im, ntohl (mp->sw_if_index));
  if (config)
    {
      /* *INDENT-OFF* */
      pool_foreach (group, config->groups, (
	{
	  pool_foreach (src, group->srcs, (
	    {
	      send_igmp_details (q, im, config, group, src, mp->context);
	    }));
	}));
      /* *INDENT-ON* */
    }
}

static void
vl_api_igmp_clear_interface_t_handler (vl_api_igmp_clear_interface_t * mp)
{
  igmp_main_t *im = &igmp_main;
  igmp_config_t *config;
  vl_api_igmp_clear_interface_reply_t *rmp;
  int rv = 0;

  config = igmp_config_lookup (im, ntohl (mp->sw_if_index));
  if (config)
    igmp_clear_config (config);

  unix_shared_memory_queue_t *q =
    vl_api_client_index_to_input_queue (mp->client_index);
  if (!q)
    return;

  rmp = vl_msg_api_alloc (sizeof (*rmp));
  rmp->_vl_msg_id =
    htons ((VL_API_IGMP_CLEAR_INTERFACE_REPLY) + im->msg_id_base);
  rmp->context = mp->context;
  rmp->retval = htonl (rv);

  vl_msg_api_send_shmem (q, (u8 *) & rmp);
}

/** \brief igmp group lookup
    @param im - igmp main
    @param client_index - client index
*/
static vpe_client_registration_t *
igmp_api_client_lookup (igmp_main_t * im, u32 client_index)
{
  uword *p;
  vpe_client_registration_t *api_client = NULL;

  p = hash_get (im->igmp_api_client_by_client_index, client_index);
  if (p)
    api_client = vec_elt_at_index (im->api_clients, p[0]);

  return api_client;
}

static void
vl_api_want_igmp_events_t_handler (vl_api_want_igmp_events_t * mp)
{
  igmp_main_t *im = &igmp_main;
  vpe_client_registration_t *api_client;
  vl_api_want_igmp_events_reply_t *rmp;
  int rv = 0;

  api_client = igmp_api_client_lookup (im, mp->client_index);
  if (api_client)
    {
      if (mp->enable)
	{
	  rv = VNET_API_ERROR_INVALID_REGISTRATION;
	  goto done;
	}
      hash_unset (im->igmp_api_client_by_client_index,
		  api_client->client_index);
      pool_put (im->api_clients, api_client);
      goto done;
    }
  if (mp->enable)
    {
      pool_get (im->api_clients, api_client);
      memset (api_client, 0, sizeof (vpe_client_registration_t));
      api_client->client_index = mp->client_index;
      api_client->client_pid = mp->pid;
      hash_set (im->igmp_api_client_by_client_index,
		mp->client_index, api_client - im->api_clients);
      goto done;
    }
  rv = VNET_API_ERROR_INVALID_REGISTRATION;

done:;
  unix_shared_memory_queue_t *q =
    vl_api_client_index_to_input_queue (mp->client_index);
  if (!q)
    return;

  rmp = vl_msg_api_alloc (sizeof (*rmp));
  rmp->_vl_msg_id = htons ((VL_API_WANT_IGMP_EVENTS_REPLY) + im->msg_id_base);
  rmp->context = mp->context;
  rmp->retval = htonl (rv);

  vl_msg_api_send_shmem (q, (u8 *) & rmp);
}

static clib_error_t *
want_igmp_events_reaper (u32 client_index)
{
  igmp_main_t *im = &igmp_main;
  vpe_client_registration_t *api_client;
  uword *p;

  p = hash_get (im->igmp_api_client_by_client_index, client_index);

  if (p)
    {
      api_client = pool_elt_at_index (im->api_clients, p[0]);
      pool_put (im->api_clients, api_client);
      hash_unset (im->igmp_api_client_by_client_index, client_index);
    }
  return (NULL);
}

VL_MSG_API_REAPER_FUNCTION (want_igmp_events_reaper);

void
send_igmp_event (unix_shared_memory_queue_t * q, u32 context,
		 igmp_main_t * im, igmp_config_t * config,
		 igmp_group_t * group, igmp_src_t * src)
{
  vl_api_igmp_event_t *mp = vl_msg_api_alloc (sizeof (*mp));
  memset (mp, 0, sizeof (*mp));

  mp->_vl_msg_id = ntohs ((VL_API_IGMP_EVENT) + im->msg_id_base);
  mp->context = context;
  mp->sw_if_index = htonl (config->sw_if_index);
  clib_memcpy (&mp->saddr, &src->addr.ip4, sizeof (ip4_address_t));
  clib_memcpy (&mp->gaddr, &group->addr.ip4, sizeof (ip4_address_t));
  mp->is_join =
    (group->type == IGMP_MEMBERSHIP_GROUP_mode_is_filter_include) ? 1 : 0;

  vl_msg_api_send_shmem (q, (u8 *) & mp);
}

void
igmp_event (igmp_main_t * im, igmp_config_t * config, igmp_group_t * group,
	    igmp_src_t * src)
{
  vpe_client_registration_t *api_client;
  unix_shared_memory_queue_t *q;
  /* *INDENT-OFF* */
  pool_foreach (api_client, im->api_clients,
    ({
      q = vl_api_client_index_to_input_queue (api_client->client_index);
      if (q)
	send_igmp_event (q, 0, im, config, group, src);
    }));
  /* *INDENT-ON* */
  if (group->type == IGMP_MEMBERSHIP_GROUP_block_old_sources)
    {
      igmp_clear_group (config, group);
      if (pool_elts (config->groups) == 0)
	{
	  hash_unset (im->igmp_config_by_sw_if_index, config->sw_if_index);
	  pool_put (im->configs, config);
	}
    }
}

#define vl_msg_name_crc_list
#include <igmp/igmp_all_api_h.h>
#undef vl_msg_name_crc_list

static void
setup_message_id_table (igmp_main_t * im, api_main_t * am)
{
#define _(id,n,crc) \
  vl_msg_api_add_msg_name_crc (am, #n "_" #crc, id + im->msg_id_base);
  foreach_vl_msg_name_crc_igmp;
#undef _
}

/* Set up the API message handling tables */
static clib_error_t *
igmp_plugin_api_hookup (vlib_main_t * vm)
{
  igmp_main_t *im = &igmp_main;
  api_main_t *am = &api_main;
  u8 *name;

  /* Construct the API name */
  name = format (0, "igmp_%08x%c", api_version, 0);

  /* Ask for a correctly-sized block of API message decode slots */
  im->msg_id_base = vl_msg_api_get_msg_ids
    ((char *) name, VL_MSG_FIRST_AVAILABLE);

#define _(N,n)                                                  \
    vl_msg_api_set_handlers((VL_API_##N + im->msg_id_base),     \
                           #n,                                  \
                           vl_api_##n##_t_handler,              \
                           vl_noop_handler,                     \
                           vl_api_##n##_t_endian,               \
                           vl_api_##n##_t_print,                \
                           sizeof(vl_api_##n##_t), 1);
  foreach_igmp_plugin_api_msg;
#undef _

  /*
   * Set up the (msg_name, crc, message-id) table
   */
  setup_message_id_table (im, am);

  vec_free (name);
  return 0;
}

VLIB_API_INIT_FUNCTION (igmp_plugin_api_hookup);

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
