/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-io.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-prph.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/fw-api.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/time-event.h"

const uint8_t iwl_mvm_ac_to_tx_fifo[] = {
    IWL_MVM_TX_FIFO_VO,
    IWL_MVM_TX_FIFO_VI,
    IWL_MVM_TX_FIFO_BE,
    IWL_MVM_TX_FIFO_BK,
};

const uint8_t iwl_mvm_ac_to_gen2_tx_fifo[] = {
    IWL_GEN2_EDCA_TX_FIFO_VO, IWL_GEN2_EDCA_TX_FIFO_VI, IWL_GEN2_EDCA_TX_FIFO_BE,
    IWL_GEN2_EDCA_TX_FIFO_BK, IWL_GEN2_TRIG_TX_FIFO_VO, IWL_GEN2_TRIG_TX_FIFO_VI,
    IWL_GEN2_TRIG_TX_FIFO_BE, IWL_GEN2_TRIG_TX_FIFO_BK,
};

struct iwl_mvm_mac_iface_iterator_data {
  struct iwl_mvm* mvm;
  struct ieee80211_vif* vif;
  unsigned available_mac_ids[BITS_TO_INTS(NUM_MAC_INDEX_DRIVER)];
  unsigned available_tsf_ids[BITS_TO_INTS(NUM_TSF_IDS)];
  enum iwl_tsf_id preferred_tsf;
  bool found_vif;
};

#if 0   // NEEDS_PORTING
static void iwl_mvm_mac_tsf_id_iter(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_mvm_mac_iface_iterator_data* data = _data;
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  uint16_t min_bi;

  /* Skip the interface for which we are trying to assign a tsf_id  */
  if (vif == data->vif) {
    return;
  }

  /*
   * The TSF is a hardware/firmware resource, there are 4 and
   * the driver should assign and free them as needed. However,
   * there are cases where 2 MACs should share the same TSF ID
   * for the purpose of clock sync, an optimization to avoid
   * clock drift causing overlapping TBTTs/DTIMs for a GO and
   * client in the system.
   *
   * The firmware will decide according to the MAC type which
   * will be the master and slave. Clients that need to sync
   * with a remote station will be the master, and an AP or GO
   * will be the slave.
   *
   * Depending on the new interface type it can be slaved to
   * or become the master of an existing interface.
   */
  switch (data->vif->type) {
    case NL80211_IFTYPE_STATION:
      /*
       * The new interface is a client, so if the one we're iterating
       * is an AP, and the beacon interval of the AP is a multiple or
       * divisor of the beacon interval of the client, the same TSF
       * should be used to avoid drift between the new client and
       * existing AP. The existing AP will get drift updates from the
       * new client context in this case.
       */
      if (vif->type != NL80211_IFTYPE_AP || data->preferred_tsf != NUM_TSF_IDS ||
          !test_bit(mvmvif->tsf_id, data->available_tsf_ids)) {
        break;
      }

      min_bi = min(data->vif->bss_conf.beacon_int, vif->bss_conf.beacon_int);

      if (!min_bi) {
        break;
      }

      if ((data->vif->bss_conf.beacon_int - vif->bss_conf.beacon_int) % min_bi == 0) {
        data->preferred_tsf = mvmvif->tsf_id;
        return;
      }
      break;

    case NL80211_IFTYPE_AP:
      /*
       * The new interface is AP/GO, so if its beacon interval is a
       * multiple or a divisor of the beacon interval of an existing
       * interface, it should get drift updates from an existing
       * client or use the same TSF as an existing GO. There's no
       * drift between TSFs internally but if they used different
       * TSFs then a new client MAC could update one of them and
       * cause drift that way.
       */
      if ((vif->type != NL80211_IFTYPE_AP && vif->type != NL80211_IFTYPE_STATION) ||
          data->preferred_tsf != NUM_TSF_IDS ||
          !test_bit(mvmvif->tsf_id, data->available_tsf_ids)) {
        break;
      }

      min_bi = min(data->vif->bss_conf.beacon_int, vif->bss_conf.beacon_int);

      if (!min_bi) {
        break;
      }

      if ((data->vif->bss_conf.beacon_int - vif->bss_conf.beacon_int) % min_bi == 0) {
        data->preferred_tsf = mvmvif->tsf_id;
        return;
      }
      break;
    default:
      /*
       * For all other interface types there's no need to
       * take drift into account. Either they're exclusive
       * like IBSS and monitor, or we don't care much about
       * their TSF (like P2P Device), but we won't be able
       * to share the TSF resource.
       */
      break;
  }

  /*
   * Unless we exited above, we can't share the TSF resource
   * that the virtual interface we're iterating over is using
   * with the new one, so clear the available bit and if this
   * was the preferred one, reset that as well.
   */
  __clear_bit(mvmvif->tsf_id, data->available_tsf_ids);

  if (data->preferred_tsf == mvmvif->tsf_id) {
    data->preferred_tsf = NUM_TSF_IDS;
  }
}

static void iwl_mvm_mac_iface_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_mvm_mac_iface_iterator_data* data = _data;
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);

  /* Iterator may already find the interface being added -- skip it */
  if (vif == data->vif) {
    data->found_vif = true;
    return;
  }

  /* Mark MAC IDs as used by clearing the available bit, and
   * (below) mark TSFs as used if their existing use is not
   * compatible with the new interface type.
   * No locking or atomic bit operations are needed since the
   * data is on the stack of the caller function.
   */
  __clear_bit(mvmvif->id, data->available_mac_ids);

  /* find a suitable tsf_id */
  iwl_mvm_mac_tsf_id_iter(_data, mac, vif);
}

void iwl_mvm_mac_ctxt_recalc_tsf_id(struct iwl_mvm* mvm, struct ieee80211_vif* vif) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_mvm_mac_iface_iterator_data data = {
      .mvm = mvm,
      .vif = vif,
      .available_tsf_ids = {(1 << NUM_TSF_IDS) - 1},
      /* no preference yet */
      .preferred_tsf = NUM_TSF_IDS,
  };

  ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
                                             iwl_mvm_mac_tsf_id_iter, &data);

  if (data.preferred_tsf != NUM_TSF_IDS) {
    mvmvif->tsf_id = data.preferred_tsf;
  } else if (!test_bit(mvmvif->tsf_id, data.available_tsf_ids)) {
    mvmvif->tsf_id = find_first_bit(data.available_tsf_ids, NUM_TSF_IDS);
  }
}
#endif  // NEEDS_PORTING

zx_status_t iwl_mvm_mac_ctxt_init(struct iwl_mvm* mvm, struct ieee80211_vif* vif) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_mvm_mac_iface_iterator_data data = {
      .mvm = mvm,
      .vif = vif,
      .available_mac_ids = {(1 << NUM_MAC_INDEX_DRIVER) - 1},  // 1 means available
      .available_tsf_ids = {(1 << NUM_TSF_IDS) - 1},           // 1 means available
      /* no preference yet */
      .preferred_tsf = NUM_TSF_IDS,
      .found_vif = false,
  };
  zx_status_t ret;
#if 0   // NEEDS_PORTING
  unsigned long used_hw_queues;
#endif  // NEEDS_PORTING

  lockdep_assert_held(&mvm->mutex);

#if 0   // NEEDS_PORTING
  // TODO(43218): Support multiple interfaces simultaneously.
  //              The current code doesn't clear bits so that always use the first interface.

  /*
   * Allocate a MAC ID and a TSF for this MAC, along with the queues
   * and other resources.
   */

  /*
   * Before the iterator, we start with all MAC IDs and TSFs available.
   *
   * During iteration, all MAC IDs are cleared that are in use by other
   * virtual interfaces, and all TSF IDs are cleared that can't be used
   * by this new virtual interface because they're used by an interface
   * that can't share it with the new one.
   * At the same time, we check if there's a preferred TSF in the case
   * that we should share it with another interface.
   */

  /* Currently, MAC ID 0 should be used only for the managed/IBSS vif */
  switch (vif->type) {
    case NL80211_IFTYPE_ADHOC:
      break;
    case NL80211_IFTYPE_STATION:
      if (!vif->p2p) {
        break;
      }
    /* fall through */
    default:
      __clear_bit(0, data.available_mac_ids);
  }

  ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
                                             iwl_mvm_mac_iface_iterator, &data);
#endif  // NEEDS_PORTING

  /*
   * In the case we're getting here during resume, it's similar to
   * firmware restart, and with RESUME_ALL the iterator will find
   * the vif being added already.
   * We don't want to reassign any IDs in either case since doing
   * so would probably assign different IDs (as interfaces aren't
   * necessarily added in the same order), but the old IDs were
   * preserved anyway, so skip ID assignment for both resume and
   * recovery.
   */
  if (data.found_vif) {
    return ZX_OK;
  }

  /* Therefore, in recovery, we can't get here */
  if (WARN_ON_ONCE(test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))) {
    return ZX_ERR_BAD_STATE;
  }

  mvmvif->id = find_first_bit(data.available_mac_ids, NUM_MAC_INDEX_DRIVER);
  if (mvmvif->id == NUM_MAC_INDEX_DRIVER) {
    IWL_ERR(mvm, "Failed to init MAC context - no free ID!\n");
    ret = ZX_ERR_IO;
    goto exit_fail;
  }

  if (data.preferred_tsf != NUM_TSF_IDS) {
    mvmvif->tsf_id = data.preferred_tsf;
  } else {
    mvmvif->tsf_id = find_first_bit(data.available_tsf_ids, NUM_TSF_IDS);
  }
  if (mvmvif->tsf_id == NUM_TSF_IDS) {
    IWL_ERR(mvm, "Failed to init MAC context - no free TSF!\n");
    ret = ZX_ERR_IO;
    goto exit_fail;
  }

  mvmvif->color = 0;

  list_initialize(&mvmvif->time_event_data.list);
  mvmvif->time_event_data.id = TE_MAX;

#if 0   // NEEDS_PORTING
  /* No need to allocate data queues to P2P Device MAC and NAN.*/
  if (vif->type == NL80211_IFTYPE_P2P_DEVICE || vif->type == NL80211_IFTYPE_NAN) {
    for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
      vif->hw_queue[ac] = IEEE80211_INVAL_HW_QUEUE;
    }

    return 0;
  }

  /*
   * queues in mac80211 almost entirely independent of
   * the ones here - no real limit
   */
  int queue_limit = IEEE80211_MAX_QUEUES;

  /*
   * Find available queues, and allocate them to the ACs. When in
   * DQA-mode they aren't really used, and this is done only so the
   * mac80211 ieee80211_check_queues() function won't fail
   */
  uint32_t ac;
  for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
    uint8_t queue = find_first_zero_bit(&used_hw_queues, queue_limit);

    if (queue >= queue_limit) {
      IWL_ERR(mvm, "Failed to allocate queue\n");
      ret = ZX_ERR_IO;
      goto exit_fail;
    }

    __set_bit(queue, &used_hw_queues);
    vif->hw_queue[ac] = queue;
  }

  /* Allocate the CAB queue for softAP and GO interfaces */
  if (vif->type == NL80211_IFTYPE_AP || vif->type == NL80211_IFTYPE_ADHOC) {
    /*
     * For TVQM this will be overwritten later with the FW assigned
     * queue value (when queue is enabled).
     */
    mvmvif->cab_queue = IWL_MVM_DQA_GCAST_QUEUE;
  }
#endif  // NEEDS_PORTING

  mvmvif->bcast_sta.sta_id = IWL_MVM_INVALID_STA;
  mvmvif->mcast_sta.sta_id = IWL_MVM_INVALID_STA;
  mvmvif->ap_sta_id = IWL_MVM_INVALID_STA;

  for (int i = 0; i < NUM_IWL_MVM_SMPS_REQ; i++) {
    mvmvif->smps_requests[i] = IEEE80211_SMPS_AUTOMATIC;
  }

  return ZX_OK;

exit_fail:
  memset(mvmvif, 0, sizeof(struct iwl_mvm_vif));
  return ret;
}

static void iwl_mvm_ack_rates(struct iwl_mvm* mvm, struct ieee80211_vif* vif, wlan_info_band_t band,
                              uint8_t* cck_rates, uint8_t* ofdm_rates) {
  // Since the 'iwl_cfg80211_rates' table is fixed, seems we can always return fixed values.
  *cck_rates = 0xf;    // 1 Mbps, 2 Mbps, 5.5 Mbps, 11 Mbps
  *ofdm_rates = 0xff;  // 6 Mbps, 9 Mbps, 12 Mbps, 18 Mbps, 24 Mbps, 36 Mbps, 48 Mbps, 54 Mbps

#if 0   // NEEDS_PORTING
  // TODO(43217): come back and review if we need the following code.
  struct ieee80211_supported_band* sband;
  unsigned long basic = vif->bss_conf.basic_rates;
  int lowest_present_ofdm = 100;
  int lowest_present_cck = 100;
  uint8_t cck = 0;
  uint8_t ofdm = 0;
  int i;

  sband = mvm->hw->wiphy->bands[band];

  for_each_set_bit(i, &basic, BITS_PER_LONG) {
    int hw = sband->bitrates[i].hw_value;
    if (hw >= IWL_FIRST_OFDM_RATE) {
      ofdm |= BIT(hw - IWL_FIRST_OFDM_RATE);
      if (lowest_present_ofdm > hw) {
        lowest_present_ofdm = hw;
      }
    } else {
      BUILD_BUG_ON(IWL_FIRST_CCK_RATE != 0);

      cck |= BIT(hw);
      if (lowest_present_cck > hw) {
        lowest_present_cck = hw;
      }
    }
  }

  /*
   * Now we've got the basic rates as bitmaps in the ofdm and cck
   * variables. This isn't sufficient though, as there might not
   * be all the right rates in the bitmap. E.g. if the only basic
   * rates are 5.5 Mbps and 11 Mbps, we still need to add 1 Mbps
   * and 6 Mbps because the 802.11-2007 standard says in 9.6:
   *
   *    [...] a STA responding to a received frame shall transmit
   *    its Control Response frame [...] at the highest rate in the
   *    BSSBasicRateSet parameter that is less than or equal to the
   *    rate of the immediately previous frame in the frame exchange
   *    sequence ([...]) and that is of the same modulation class
   *    ([...]) as the received frame. If no rate contained in the
   *    BSSBasicRateSet parameter meets these conditions, then the
   *    control frame sent in response to a received frame shall be
   *    transmitted at the highest mandatory rate of the PHY that is
   *    less than or equal to the rate of the received frame, and
   *    that is of the same modulation class as the received frame.
   *
   * As a consequence, we need to add all mandatory rates that are
   * lower than all of the basic rates to these bitmaps.
   */

  if (IWL_RATE_24M_INDEX < lowest_present_ofdm) {
    ofdm |= IWL_RATE_BIT_MSK(24) >> IWL_FIRST_OFDM_RATE;
  }
  if (IWL_RATE_12M_INDEX < lowest_present_ofdm) {
    ofdm |= IWL_RATE_BIT_MSK(12) >> IWL_FIRST_OFDM_RATE;
  }
  /* 6M already there or needed so always add */
  ofdm |= IWL_RATE_BIT_MSK(6) >> IWL_FIRST_OFDM_RATE;

  /*
   * CCK is a bit more complex with DSSS vs. HR/DSSS vs. ERP.
   * Note, however:
   *  - if no CCK rates are basic, it must be ERP since there must
   *    be some basic rates at all, so they're OFDM => ERP PHY
   *    (or we're in 5 GHz, and the cck bitmap will never be used)
   *  - if 11M is a basic rate, it must be ERP as well, so add 5.5M
   *  - if 5.5M is basic, 1M and 2M are mandatory
   *  - if 2M is basic, 1M is mandatory
   *  - if 1M is basic, that's the only valid ACK rate.
   * As a consequence, it's not as complicated as it sounds, just add
   * any lower rates to the ACK rate bitmap.
   */
  if (IWL_RATE_11M_INDEX < lowest_present_cck) {
    cck |= IWL_RATE_BIT_MSK(11) >> IWL_FIRST_CCK_RATE;
  }
  if (IWL_RATE_5M_INDEX < lowest_present_cck) {
    cck |= IWL_RATE_BIT_MSK(5) >> IWL_FIRST_CCK_RATE;
  }
  if (IWL_RATE_2M_INDEX < lowest_present_cck) {
    cck |= IWL_RATE_BIT_MSK(2) >> IWL_FIRST_CCK_RATE;
  }
  /* 1M already there or needed so always add */
  cck |= IWL_RATE_BIT_MSK(1) >> IWL_FIRST_CCK_RATE;

  *cck_rates = cck;
  *ofdm_rates = ofdm;
#endif  // NEEDS_PORTING
}

static void iwl_mvm_mac_ctxt_set_ht_flags(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                          struct iwl_mac_ctx_cmd* cmd) {
  // Use conservative value for better compatibility.
  // TODO(43248): optimize this.
  cmd->protection_flags |= cpu_to_le32(MAC_PROT_FLG_HT_PROT | MAC_PROT_FLG_FAT_PROT);

#if 0   // NEEDS_PORTING
  /* for both sta and ap, ht_operation_mode hold the protection_mode */
  uint8_t protection_mode = vif->bss_conf.ht_operation_mode & IEEE80211_HT_OP_MODE_PROTECTION;
  /* The fw does not distinguish between ht and fat */
  uint32_t ht_flag = MAC_PROT_FLG_HT_PROT | MAC_PROT_FLG_FAT_PROT;

  IWL_DEBUG_RATE(mvm, "protection mode set to %d\n", protection_mode);
  /*
   * See section 9.23.3.1 of IEEE 80211-2012.
   * Nongreenfield HT STAs Present is not supported.
   */
  switch (protection_mode) {
    case IEEE80211_HT_OP_MODE_PROTECTION_NONE:
      break;
    case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
    case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
      cmd->protection_flags |= cpu_to_le32(ht_flag);
      break;
    case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
      /* Protect when channel wider than 20MHz */
      if (vif->bss_conf.chandef.width > NL80211_CHAN_WIDTH_20) {
        cmd->protection_flags |= cpu_to_le32(ht_flag);
      }
      break;
    default:
      IWL_ERR(mvm, "Illegal protection mode %d\n", protection_mode);
      break;
  }
#endif  // NEEDS_PORTING
}

static void iwl_mvm_mac_ctxt_cmd_common(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                        wlan_info_band_t band, bool ht_enabled,
                                        struct iwl_mac_ctx_cmd* cmd, const uint8_t* bssid_override,
                                        uint32_t action) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  uint8_t cck_ack_rates, ofdm_ack_rates;
  const uint8_t* bssid = bssid_override ?: vif->bss_conf.bssid;

  cmd->id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));
  cmd->action = cpu_to_le32(action);

  switch (vif->type) {
    case WLAN_INFO_MAC_ROLE_CLIENT:
      cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_BSS_STA);
      break;
    case WLAN_INFO_MAC_ROLE_AP:
      cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_GO);
      break;
#if 0   // NEEDS_PORTING
    case NL80211_IFTYPE_MONITOR:
      cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_LISTENER);
      break;
    case NL80211_IFTYPE_P2P_DEVICE:
      cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_P2P_DEVICE);
      break;
    case NL80211_IFTYPE_ADHOC:
      cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_IBSS);
      break;
#endif  // NEEDS_PORTING
    default:
      IWL_ERR(mvm, "%s(): unknown vif->type: %d\n", __func__, vif->type);
  }

  cmd->tsf_id = cpu_to_le32(mvmvif->tsf_id);

  memcpy(cmd->node_addr, vif->addr, ETH_ALEN);

  if (bssid) {
    memcpy(cmd->bssid_addr, bssid, ETH_ALEN);
  } else {
    eth_broadcast_addr(cmd->bssid_addr);
  }

  rcu_read_lock();
  iwl_mvm_ack_rates(mvm, vif, band, &cck_ack_rates, &ofdm_ack_rates);
  rcu_read_unlock();

  cmd->cck_rates = cpu_to_le32((uint32_t)cck_ack_rates);
  cmd->ofdm_rates = cpu_to_le32((uint32_t)ofdm_ack_rates);

  cmd->cck_short_preamble =
      cpu_to_le32(vif->bss_conf.use_short_preamble ? MAC_FLG_SHORT_PREAMBLE : 0);
  cmd->short_slot = cpu_to_le32(vif->bss_conf.use_short_slot ? MAC_FLG_SHORT_SLOT : 0);

  cmd->filter_flags = cpu_to_le32(MAC_FILTER_ACCEPT_GRP);

  for (int i = 0; i < IEEE80211_NUM_ACS; i++) {
    uint8_t txf = iwl_mvm_mac_ac_to_tx_fifo(mvm, i);

    cmd->ac[txf].cw_min = cpu_to_le16(mvmvif->queue_params[i].cw_min);
    cmd->ac[txf].cw_max = cpu_to_le16(mvmvif->queue_params[i].cw_max);
    cmd->ac[txf].edca_txop = cpu_to_le16(mvmvif->queue_params[i].txop * 32);
    cmd->ac[txf].aifsn = mvmvif->queue_params[i].aifs;
    cmd->ac[txf].fifos_mask = BIT(txf);
  }

  if (vif->bss_conf.qos) {
    cmd->qos_flags |= cpu_to_le32(MAC_QOS_FLG_UPDATE_EDCA);
  }

  if (vif->bss_conf.use_cts_prot) {
    cmd->protection_flags |= cpu_to_le32(MAC_PROT_FLG_TGG_PROTECT);
  }

  IWL_DEBUG_RATE(mvm, "use_cts_prot %d, ht_operation_mode %d\n", vif->bss_conf.use_cts_prot,
                 vif->bss_conf.ht_operation_mode);
  if (vif->bss_conf.chandef.cbw != WLAN_CHANNEL_BANDWIDTH__20) {
    cmd->qos_flags |= cpu_to_le32(MAC_QOS_FLG_TGN);
  }
  if (ht_enabled) {
    iwl_mvm_mac_ctxt_set_ht_flags(mvm, vif, cmd);
  }
}

static zx_status_t iwl_mvm_mac_ctxt_send_cmd(struct iwl_mvm* mvm, struct iwl_mac_ctx_cmd* cmd) {
  zx_status_t ret = iwl_mvm_send_cmd_pdu(mvm, MAC_CONTEXT_CMD, 0, sizeof(*cmd), cmd);
  if (ret) {
    IWL_ERR(mvm, "Failed to send MAC context (action:%d): %d\n", le32_to_cpu(cmd->action), ret);
  }
  return ret;
}

static zx_status_t iwl_mvm_mac_ctxt_cmd_sta(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                            uint32_t action, bool force_assoc_off,
                                            const uint8_t* bssid_override) {
  struct iwl_mac_ctx_cmd cmd = {};
  struct iwl_mac_data_sta* ctxt_sta;

  WARN_ON(vif->type != WLAN_INFO_MAC_ROLE_CLIENT);

  /* Fill the common data for all mac context types */
  iwl_mvm_mac_ctxt_cmd_common(mvm, vif, WLAN_INFO_BAND_2GHZ,  // Use default value.
                              vif->ht_enabled, &cmd, bssid_override, action);

  // Fill in dummy values to add interface for scanning.
  // TODO(37593): Re-write the NEEDS_PORTING section below for association.
  cmd.filter_flags |= cpu_to_le32(MAC_FILTER_IN_BEACON);
  ctxt_sta = &cmd.sta;
  ctxt_sta->is_assoc = cpu_to_le32(0);
  ctxt_sta->bi = cpu_to_le32(100);
  ctxt_sta->dtim_interval = cpu_to_le32(300);
  ctxt_sta->listen_interval = cpu_to_le32(8);
  ctxt_sta->assoc_id = cpu_to_le32(0);

#if 0  // NEEDS_PORTING
  if (vif->p2p) {
    struct ieee80211_p2p_noa_attr* noa = &vif->bss_conf.p2p_noa_attr;
#ifdef CPTCFG_IWLMVM_P2P_OPPPS_TEST_WA
    /*
     * Pass CT window including OPPPS enable flag as part of a WA
     * to pass P2P OPPPS certification test. Refer to
     * IWLMVM_P2P_OPPPS_TEST_WA description in Kconfig.noupstream.
     */
    if (mvm->p2p_opps_test_wa_vif) {
      cmd.p2p_sta.ctwin = cpu_to_le32(noa->oppps_ctwindow);
    } else
#endif
      cmd.p2p_sta.ctwin = cpu_to_le32(noa->oppps_ctwindow & IEEE80211_P2P_OPPPS_CTWINDOW_MASK);
    ctxt_sta = &cmd.p2p_sta.sta;
  } else {
    ctxt_sta = &cmd.sta;
  }

  /* We need the dtim_period to set the MAC as associated */
  if (vif->bss_conf.assoc && vif->bss_conf.dtim_period && !force_assoc_off) {
    uint32_t dtim_offs;

    /*
     * The DTIM count counts down, so when it is N that means N
     * more beacon intervals happen until the DTIM TBTT. Therefore
     * add this to the current time. If that ends up being in the
     * future, the firmware will handle it.
     *
     * Also note that the system_timestamp (which we get here as
     * "sync_device_ts") and TSF timestamp aren't at exactly the
     * same offset in the frame -- the TSF is at the first symbol
     * of the TSF, the system timestamp is at signal acquisition
     * time. This means there's an offset between them of at most
     * a few hundred microseconds (24 * 8 bits + PLCP time gives
     * 384us in the longest case), this is currently not relevant
     * as the firmware wakes up around 2ms before the TBTT.
     */
    dtim_offs = vif->bss_conf.sync_dtim_count * vif->bss_conf.beacon_int;
    /* convert TU to usecs */
    dtim_offs *= 1024;

    ctxt_sta->dtim_tsf = cpu_to_le64(vif->bss_conf.sync_tsf + dtim_offs);
    ctxt_sta->dtim_time = cpu_to_le32(vif->bss_conf.sync_device_ts + dtim_offs);
    ctxt_sta->assoc_beacon_arrive_time = cpu_to_le32(vif->bss_conf.sync_device_ts);

    IWL_DEBUG_INFO(mvm, "DTIM TBTT is 0x%llx/0x%x, offset %d\n", le64_to_cpu(ctxt_sta->dtim_tsf),
                   le32_to_cpu(ctxt_sta->dtim_time), dtim_offs);

    ctxt_sta->is_assoc = cpu_to_le32(1);
  } else {
    ctxt_sta->is_assoc = cpu_to_le32(0);

    /* Allow beacons to pass through as long as we are not
     * associated, or we do not have dtim period information.
     */
    cmd.filter_flags |= cpu_to_le32(MAC_FILTER_IN_BEACON);
  }

  ctxt_sta->bi = cpu_to_le32(vif->bss_conf.beacon_int);
  ctxt_sta->dtim_interval = cpu_to_le32(vif->bss_conf.beacon_int * vif->bss_conf.dtim_period);

  ctxt_sta->listen_interval = cpu_to_le32(mvm->hw->conf.listen_interval);
  ctxt_sta->assoc_id = cpu_to_le32(vif->bss_conf.aid);

  if (vif->probe_req_reg && vif->bss_conf.assoc && vif->p2p) {
    cmd.filter_flags |= cpu_to_le32(MAC_FILTER_IN_PROBE_REQUEST);
  }

  if (vif->bss_conf.assoc && vif->bss_conf.he_support && !iwlwifi_mod_params.disable_11ax) {
    cmd.filter_flags |= cpu_to_le32(MAC_FILTER_IN_11AX);
    if (vif->bss_conf.twt_requester) {
      ctxt_sta->data_policy |= cpu_to_le32(TWT_SUPPORTED);
    }
  }
#endif  // NEEDS_PORTING

  return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

#if 0  // NEEDS_PORTING
static int iwl_mvm_mac_ctxt_cmd_listener(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                         uint32_t action) {
  struct iwl_mac_ctx_cmd cmd = {};
  uint32_t tfd_queue_msk = BIT(mvm->snif_queue);
  int ret;

  WARN_ON(vif->type != NL80211_IFTYPE_MONITOR);

  iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, NULL, action);

  cmd.filter_flags =
      cpu_to_le32(MAC_FILTER_IN_PROMISC | MAC_FILTER_IN_CONTROL_AND_MGMT | MAC_FILTER_IN_BEACON |
                  MAC_FILTER_IN_PROBE_REQUEST | MAC_FILTER_IN_CRC32);
  ieee80211_hw_set(mvm->hw, RX_INCLUDES_FCS);

  /* Allocate sniffer station */
  ret = iwl_mvm_allocate_int_sta(mvm, &mvm->snif_sta, tfd_queue_msk, vif->type,
                                 IWL_STA_GENERAL_PURPOSE);
  if (ret) {
    return ret;
  }

  return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mac_ctxt_cmd_ibss(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                     uint32_t action) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_mac_ctx_cmd cmd = {};

  WARN_ON(vif->type != NL80211_IFTYPE_ADHOC);

  iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, NULL, action);

  cmd.filter_flags = cpu_to_le32(MAC_FILTER_IN_BEACON | MAC_FILTER_IN_PROBE_REQUEST);

  /* cmd.ibss.beacon_time/cmd.ibss.beacon_tsf are curently ignored */
  cmd.ibss.bi = cpu_to_le32(vif->bss_conf.beacon_int);

  /* TODO: Assumes that the beacon id == mac context id */
  cmd.ibss.beacon_template = cpu_to_le32(mvmvif->id);

  return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

struct iwl_mvm_go_iterator_data {
  bool go_active;
};

static void iwl_mvm_go_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_mvm_go_iterator_data* data = _data;
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);

  if (vif->type == NL80211_IFTYPE_AP && vif->p2p && mvmvif->ap_ibss_active) {
    data->go_active = true;
  }
}

static int iwl_mvm_mac_ctxt_cmd_p2p_device(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                           uint32_t action) {
  struct iwl_mac_ctx_cmd cmd = {};
  struct iwl_mvm_go_iterator_data data = {};

  WARN_ON(vif->type != NL80211_IFTYPE_P2P_DEVICE);

  iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, NULL, action);

  /* Override the filter flags to accept only probe requests */
  cmd.filter_flags = cpu_to_le32(MAC_FILTER_IN_PROBE_REQUEST);

  /*
   * This flag should be set to true when the P2P Device is
   * discoverable and there is at least another active P2P GO. Settings
   * this flag will allow the P2P Device to be discoverable on other
   * channels in addition to its listen channel.
   * Note that this flag should not be set in other cases as it opens the
   * Rx filters on all MAC and increases the number of interrupts.
   */
  ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
                                             iwl_mvm_go_iterator, &data);

  cmd.p2p_dev.is_disc_extended = cpu_to_le32(data.go_active ? 1 : 0);
  return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

static void iwl_mvm_mac_ctxt_set_tim(struct iwl_mvm* mvm, __le32* tim_index, __le32* tim_size,
                                     uint8_t* beacon, uint32_t frame_size) {
  uint32_t tim_idx;
  struct ieee80211_mgmt* mgmt = (struct ieee80211_mgmt*)beacon;

  /* The index is relative to frame start but we start looking at the
   * variable-length part of the beacon. */
  tim_idx = mgmt->u.beacon.variable - beacon;

  /* Parse variable-length elements of beacon to find WLAN_EID_TIM */
  while ((tim_idx < (frame_size - 2)) && (beacon[tim_idx] != WLAN_EID_TIM)) {
    tim_idx += beacon[tim_idx + 1] + 2;
  }

  /* If TIM field was found, set variables */
  if ((tim_idx < (frame_size - 1)) && (beacon[tim_idx] == WLAN_EID_TIM)) {
    *tim_index = cpu_to_le32(tim_idx);
    *tim_size = cpu_to_le32((uint32_t)beacon[tim_idx + 1]);
  } else {
    IWL_WARN(mvm, "Unable to find TIM Element in beacon\n");
  }
}

static uint32_t iwl_mvm_find_ie_offset(uint8_t* beacon, uint8_t eid, uint32_t frame_size) {
  struct ieee80211_mgmt* mgmt = (void*)beacon;
  const uint8_t* ie;

  if (WARN_ON_ONCE(frame_size <= (mgmt->u.beacon.variable - beacon))) {
    return 0;
  }

  frame_size -= mgmt->u.beacon.variable - beacon;

  ie = cfg80211_find_ie(eid, mgmt->u.beacon.variable, frame_size);
  if (!ie) {
    return 0;
  }

  return ie - beacon;
}

static uint8_t iwl_mvm_mac_ctxt_get_lowest_rate(struct ieee80211_tx_info* info,
                                                struct ieee80211_vif* vif) {
  uint8_t rate;

  if (info->band == NL80211_BAND_5GHZ || vif->p2p) {
    rate = IWL_FIRST_OFDM_RATE;
  } else {
    rate = IWL_FIRST_CCK_RATE;
  }

#ifdef CPTCFG_IWLWIFI_FORCE_OFDM_RATE
  rate = IWL_FIRST_OFDM_RATE;
#endif
  return rate;
}

static void iwl_mvm_mac_ctxt_set_tx(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                    struct sk_buff* beacon, struct iwl_tx_cmd* tx) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct ieee80211_tx_info* info;
  uint8_t rate;
  uint32_t tx_flags;

  info = IEEE80211_SKB_CB(beacon);

  /* Set up TX command fields */
  tx->len = cpu_to_le16((uint16_t)beacon->len);
  tx->sta_id = mvmvif->bcast_sta.sta_id;
  tx->life_time = cpu_to_le32(TX_CMD_LIFE_TIME_INFINITE);
  tx_flags = TX_CMD_FLG_SEQ_CTL | TX_CMD_FLG_TSF;
  tx_flags |= iwl_mvm_bt_coex_tx_prio(mvm, (void*)beacon->data, info, 0) << TX_CMD_FLG_BT_PRIO_POS;
  tx->tx_flags = cpu_to_le32(tx_flags);

  /*
   * TODO: the firwmare advertises this, but has a bug. We should revert
   *   this when the firmware will be fixed.
   */
  if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_BEACON_ANT_SELECTION) || true) {
    iwl_mvm_toggle_tx_ant(mvm, &mvm->mgmt_last_antenna_idx);

    tx->rate_n_flags = cpu_to_le32(BIT(mvm->mgmt_last_antenna_idx) << RATE_MCS_ANT_POS);
  }

  rate = iwl_mvm_mac_ctxt_get_lowest_rate(info, vif);

  tx->rate_n_flags |= cpu_to_le32(iwl_mvm_mac80211_idx_to_hwrate(rate));
  if (rate == IWL_FIRST_CCK_RATE) {
    tx->rate_n_flags |= cpu_to_le32(RATE_MCS_CCK_MSK);
  }
}

static int iwl_mvm_mac_ctxt_send_beacon_cmd(struct iwl_mvm* mvm, struct sk_buff* beacon, void* data,
                                            int len) {
  struct iwl_host_cmd cmd = {
      .id = BEACON_TEMPLATE_CMD,
      .flags = CMD_ASYNC,
  };

  cmd.len[0] = len;
  cmd.data[0] = data;
  cmd.dataflags[0] = 0;
  cmd.len[1] = beacon->len;
  cmd.data[1] = beacon->data;
  cmd.dataflags[1] = IWL_HCMD_DFL_DUP;

  return iwl_mvm_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mac_ctxt_send_beacon_v6(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                           struct sk_buff* beacon) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_mac_beacon_cmd_v6 beacon_cmd = {};

  iwl_mvm_mac_ctxt_set_tx(mvm, vif, beacon, &beacon_cmd.tx);

  beacon_cmd.template_id = cpu_to_le32((uint32_t)mvmvif->id);

  if (vif->type == NL80211_IFTYPE_AP)
    iwl_mvm_mac_ctxt_set_tim(mvm, &beacon_cmd.tim_idx, &beacon_cmd.tim_size, beacon->data,
                             beacon->len);

  return iwl_mvm_mac_ctxt_send_beacon_cmd(mvm, beacon, &beacon_cmd, sizeof(beacon_cmd));
}

static int iwl_mvm_mac_ctxt_send_beacon_v7(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                           struct sk_buff* beacon) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_mac_beacon_cmd_v7 beacon_cmd = {};

  iwl_mvm_mac_ctxt_set_tx(mvm, vif, beacon, &beacon_cmd.tx);

  beacon_cmd.template_id = cpu_to_le32((uint32_t)mvmvif->id);

  if (vif->type == NL80211_IFTYPE_AP)
    iwl_mvm_mac_ctxt_set_tim(mvm, &beacon_cmd.tim_idx, &beacon_cmd.tim_size, beacon->data,
                             beacon->len);

  beacon_cmd.csa_offset =
      cpu_to_le32(iwl_mvm_find_ie_offset(beacon->data, WLAN_EID_CHANNEL_SWITCH, beacon->len));
  beacon_cmd.ecsa_offset =
      cpu_to_le32(iwl_mvm_find_ie_offset(beacon->data, WLAN_EID_EXT_CHANSWITCH_ANN, beacon->len));

  return iwl_mvm_mac_ctxt_send_beacon_cmd(mvm, beacon, &beacon_cmd, sizeof(beacon_cmd));
}

static int iwl_mvm_mac_ctxt_send_beacon_v9(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                           struct sk_buff* beacon) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct ieee80211_tx_info* info = IEEE80211_SKB_CB(beacon);
  struct iwl_mac_beacon_cmd beacon_cmd = {};
  uint8_t rate = iwl_mvm_mac_ctxt_get_lowest_rate(info, vif);
  uint16_t flags;

  flags = iwl_mvm_mac80211_idx_to_hwrate(rate);

  if (rate == IWL_FIRST_CCK_RATE) {
    flags |= IWL_MAC_BEACON_CCK;
  }

  beacon_cmd.flags = cpu_to_le16(flags);
  beacon_cmd.byte_cnt = cpu_to_le16((uint16_t)beacon->len);
  beacon_cmd.template_id = cpu_to_le32((uint32_t)mvmvif->id);

  if (vif->type == NL80211_IFTYPE_AP)
    iwl_mvm_mac_ctxt_set_tim(mvm, &beacon_cmd.tim_idx, &beacon_cmd.tim_size, beacon->data,
                             beacon->len);

  beacon_cmd.csa_offset =
      cpu_to_le32(iwl_mvm_find_ie_offset(beacon->data, WLAN_EID_CHANNEL_SWITCH, beacon->len));
  beacon_cmd.ecsa_offset =
      cpu_to_le32(iwl_mvm_find_ie_offset(beacon->data, WLAN_EID_EXT_CHANSWITCH_ANN, beacon->len));

  return iwl_mvm_mac_ctxt_send_beacon_cmd(mvm, beacon, &beacon_cmd, sizeof(beacon_cmd));
}

static int iwl_mvm_mac_ctxt_send_beacon(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                        struct sk_buff* beacon) {
  if (WARN_ON(!beacon)) {
    return -EINVAL;
  }

  if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_CSA_AND_TBTT_OFFLOAD)) {
    return iwl_mvm_mac_ctxt_send_beacon_v6(mvm, vif, beacon);
  }

  /* TODO: remove first condition once FW merge new TLV */
  if (iwl_mvm_has_new_tx_api(mvm) ||
      fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_NEW_BEACON_TEMPLATE)) {
    return iwl_mvm_mac_ctxt_send_beacon_v9(mvm, vif, beacon);
  }

  return iwl_mvm_mac_ctxt_send_beacon_v7(mvm, vif, beacon);
}

/* The beacon template for the AP/GO/IBSS has changed and needs update */
int iwl_mvm_mac_ctxt_beacon_changed(struct iwl_mvm* mvm, struct ieee80211_vif* vif) {
  struct sk_buff* beacon;
  int ret;

  WARN_ON(vif->type != NL80211_IFTYPE_AP && vif->type != NL80211_IFTYPE_ADHOC);

  beacon = ieee80211_beacon_get_template(mvm->hw, vif, NULL);
  if (!beacon) {
    return -ENOMEM;
  }

  ret = iwl_mvm_mac_ctxt_send_beacon(mvm, vif, beacon);
  dev_kfree_skb(beacon);
  return ret;
}

struct iwl_mvm_mac_ap_iterator_data {
  struct iwl_mvm* mvm;
  struct ieee80211_vif* vif;
  uint32_t beacon_device_ts;
  uint16_t beacon_int;
};

/* Find the beacon_device_ts and beacon_int for a managed interface */
static void iwl_mvm_mac_ap_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_mvm_mac_ap_iterator_data* data = _data;

  if (vif->type != NL80211_IFTYPE_STATION || !vif->bss_conf.assoc) {
    return;
  }

  /* Station client has higher priority over P2P client*/
  if (vif->p2p && data->beacon_device_ts) {
    return;
  }

  data->beacon_device_ts = vif->bss_conf.sync_device_ts;
  data->beacon_int = vif->bss_conf.beacon_int;
}

/*
 * Fill the specific data for mac context of type AP of P2P GO
 */
static void iwl_mvm_mac_ctxt_cmd_fill_ap(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                         struct iwl_mac_ctx_cmd* cmd,
                                         struct iwl_mac_data_ap* ctxt_ap, bool add) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_mvm_mac_ap_iterator_data data = {.mvm = mvm, .vif = vif, .beacon_device_ts = 0};

  /* in AP mode, the MCAST FIFO takes the EDCA params from VO */
  cmd->ac[IWL_MVM_TX_FIFO_VO].fifos_mask |= BIT(IWL_MVM_TX_FIFO_MCAST);

  /*
   * in AP mode, pass probe requests and beacons from other APs
   * (needed for ht protection); when there're no any associated
   * station don't ask FW to pass beacons to prevent unnecessary
   * wake-ups.
   */
  cmd->filter_flags |= cpu_to_le32(MAC_FILTER_IN_PROBE_REQUEST);
  if (mvmvif->ap_assoc_sta_count || !mvm->drop_bcn_ap_mode) {
    cmd->filter_flags |= cpu_to_le32(MAC_FILTER_IN_BEACON);
    IWL_DEBUG_HC(mvm, "Asking FW to pass beacons\n");
  } else {
    IWL_DEBUG_HC(mvm, "No need to receive beacons\n");
  }

  if (vif->bss_conf.he_support && !iwlwifi_mod_params.disable_11ax) {
    cmd->filter_flags |= cpu_to_le32(MAC_FILTER_IN_11AX);
  }

  ctxt_ap->bi = cpu_to_le32(vif->bss_conf.beacon_int);
  ctxt_ap->dtim_interval = cpu_to_le32(vif->bss_conf.beacon_int * vif->bss_conf.dtim_period);

  if (!fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_STA_TYPE)) {
    ctxt_ap->mcast_qid = cpu_to_le32(mvmvif->cab_queue);
  }

  /*
   * Only set the beacon time when the MAC is being added, when we
   * just modify the MAC then we should keep the time -- the firmware
   * can otherwise have a "jumping" TBTT.
   */
  if (add) {
    /*
     * If there is a station/P2P client interface which is
     * associated, set the AP's TBTT far enough from the station's
     * TBTT. Otherwise, set it to the current system time
     */
    ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
                                               iwl_mvm_mac_ap_iterator, &data);

    if (data.beacon_device_ts) {
      uint32_t rand = (prandom_u32() % (64 - 36)) + 36;
      mvmvif->ap_beacon_time =
          data.beacon_device_ts + ieee80211_tu_to_usec(data.beacon_int * rand / 100);
    } else {
      mvmvif->ap_beacon_time = iwl_read_prph(mvm->trans, DEVICE_SYSTEM_TIME_REG);
    }
  }

  ctxt_ap->beacon_time = cpu_to_le32(mvmvif->ap_beacon_time);
  ctxt_ap->beacon_tsf = 0; /* unused */

  /* TODO: Assume that the beacon id == mac context id */
  ctxt_ap->beacon_template = cpu_to_le32(mvmvif->id);
}

static int iwl_mvm_mac_ctxt_cmd_ap(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                   uint32_t action) {
  struct iwl_mac_ctx_cmd cmd = {};

  WARN_ON(vif->type != NL80211_IFTYPE_AP || vif->p2p);

  /* Fill the common data for all mac context types */
  iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, NULL, action);

  /* Fill the data specific for ap mode */
  iwl_mvm_mac_ctxt_cmd_fill_ap(mvm, vif, &cmd, &cmd.ap, action == FW_CTXT_ACTION_ADD);

  return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mac_ctxt_cmd_go(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                   uint32_t action) {
  struct iwl_mac_ctx_cmd cmd = {};
  struct ieee80211_p2p_noa_attr* noa = &vif->bss_conf.p2p_noa_attr;

  WARN_ON(vif->type != NL80211_IFTYPE_AP || !vif->p2p);

  /* Fill the common data for all mac context types */
  iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, NULL, action);

  /* Fill the data specific for GO mode */
  iwl_mvm_mac_ctxt_cmd_fill_ap(mvm, vif, &cmd, &cmd.go.ap, action == FW_CTXT_ACTION_ADD);

  cmd.go.ctwin = cpu_to_le32(noa->oppps_ctwindow & IEEE80211_P2P_OPPPS_CTWINDOW_MASK);
  cmd.go.opp_ps_enabled = cpu_to_le32(!!(noa->oppps_ctwindow & IEEE80211_P2P_OPPPS_ENABLE_BIT));

  return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}
#endif  // NEEDS_PORTING

static zx_status_t iwl_mvm_mac_ctx_send(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                        uint32_t action, bool force_assoc_off,
                                        const uint8_t* bssid_override) {
  switch (vif->type) {
    case WLAN_INFO_MAC_ROLE_CLIENT:
      return iwl_mvm_mac_ctxt_cmd_sta(mvm, vif, action, force_assoc_off, bssid_override);
      break;
#if 0   // NEEDS_PORTING
    case NL80211_IFTYPE_AP:
      if (!vif->p2p) {
        return iwl_mvm_mac_ctxt_cmd_ap(mvm, vif, action);
      } else {
        return iwl_mvm_mac_ctxt_cmd_go(mvm, vif, action);
      }
      break;
    case NL80211_IFTYPE_MONITOR:
      return iwl_mvm_mac_ctxt_cmd_listener(mvm, vif, action);
    case NL80211_IFTYPE_P2P_DEVICE:
      return iwl_mvm_mac_ctxt_cmd_p2p_device(mvm, vif, action);
    case NL80211_IFTYPE_ADHOC:
      return iwl_mvm_mac_ctxt_cmd_ibss(mvm, vif, action);
#endif  // NEEDS_PORTING
    default:
      break;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t iwl_mvm_mac_ctxt_add(struct iwl_mvm* mvm, struct ieee80211_vif* vif) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  zx_status_t ret;

#if 0   // NEEDS_PORTING
  if (WARN_ON_ONCE(vif->type == NL80211_IFTYPE_NAN)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
#endif  // NEEDS_PORTING

  if (mvmvif->uploaded) {
    IWL_ERR(mvm, "Adding active MAC\n");
    return ZX_ERR_IO;
  }

  ret = iwl_mvm_mac_ctx_send(mvm, vif, FW_CTXT_ACTION_ADD, true, NULL);
  if (ret != ZX_OK) {
    return ret;
  }

  /* will only do anything at resume from D3 time */
  iwl_mvm_set_last_nonqos_seq(mvm, vif);

  mvmvif->uploaded = true;
  return ZX_OK;
}

zx_status_t iwl_mvm_mac_ctxt_changed(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                     bool force_assoc_off, const uint8_t* bssid_override) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);

#if 0   // NEEDS_PORTING
  if (WARN_ON_ONCE(vif->type == NL80211_IFTYPE_NAN)) {
    return -EOPNOTSUPP;
  }
#endif  // NEEDS_PORTING

  if (!mvmvif->uploaded) {
    IWL_ERR(mvm, "Changing inactive MAC\n");
    return ZX_ERR_IO;
  }

  return iwl_mvm_mac_ctx_send(mvm, vif, FW_CTXT_ACTION_MODIFY, force_assoc_off, bssid_override);
}

zx_status_t iwl_mvm_mac_ctxt_remove(struct iwl_mvm* mvm, struct ieee80211_vif* vif) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_mac_ctx_cmd cmd;
  zx_status_t ret;

#if 0   // NEEDS_PORTING
  if (WARN_ON_ONCE(vif->type == NL80211_IFTYPE_NAN)) {
    return -EOPNOTSUPP;
  }
#endif  // NEEDS_PORTING

  if (!mvmvif->uploaded) {
    IWL_ERR(mvm, "Removing inactive MAC\n");
    return ZX_ERR_IO;
  }

  memset(&cmd, 0, sizeof(cmd));

  cmd.id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));
  cmd.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE);

  ret = iwl_mvm_send_cmd_pdu(mvm, MAC_CONTEXT_CMD, 0, sizeof(cmd), &cmd);
  if (ret) {
    IWL_ERR(mvm, "Failed to remove MAC context: %d\n", ret);
    return ret;
  }

  mvmvif->uploaded = false;

#if 0   // NEEDS_PORTING
  if (vif->type == NL80211_IFTYPE_MONITOR) {
    __clear_bit(IEEE80211_HW_RX_INCLUDES_FCS, mvm->hw->flags);
    iwl_mvm_dealloc_snif_sta(mvm);
  }
#endif  // NEEDS_PORTING

  return ZX_OK;
}

#if 0   // NEEDS_PORTING
static void iwl_mvm_csa_count_down(struct iwl_mvm* mvm, struct ieee80211_vif* csa_vif, uint32_t gp2,
                                   bool tx_success) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(csa_vif);

  /* Don't start to countdown from a failed beacon */
  if (!tx_success && !mvmvif->csa_countdown) {
    return;
  }

  mvmvif->csa_countdown = true;

  if (!ieee80211_csa_is_complete(csa_vif)) {
    int c = ieee80211_csa_update_counter(csa_vif);

    iwl_mvm_mac_ctxt_beacon_changed(mvm, csa_vif);
    if (csa_vif->p2p && !iwl_mvm_te_scheduled(&mvmvif->time_event_data) && gp2 && tx_success) {
      uint32_t rel_time = (c + 1) * csa_vif->bss_conf.beacon_int - IWL_MVM_CHANNEL_SWITCH_TIME_GO;
      uint32_t apply_time = gp2 + rel_time * 1024;

      iwl_mvm_schedule_csa_period(
          mvm, csa_vif, IWL_MVM_CHANNEL_SWITCH_TIME_GO - IWL_MVM_CHANNEL_SWITCH_MARGIN, apply_time);
    }
  } else if (!iwl_mvm_te_scheduled(&mvmvif->time_event_data)) {
    /* we don't have CSA NoA scheduled yet, switch now */
    ieee80211_csa_finish(csa_vif);
    RCU_INIT_POINTER(mvm->csa_vif, NULL);
  }
}

void iwl_mvm_rx_beacon_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  struct iwl_extended_beacon_notif* beacon = (void*)pkt->data;
  struct iwl_mvm_tx_resp* beacon_notify_hdr;
  struct ieee80211_vif* csa_vif;
  struct ieee80211_vif* tx_blocked_vif;
  struct agg_tx_status* agg_status;
  uint16_t status;

  lockdep_assert_held(&mvm->mutex);

  beacon_notify_hdr = &beacon->beacon_notify_hdr;
  mvm->ap_last_beacon_gp2 = le32_to_cpu(beacon->gp2);
  mvm->ibss_manager = beacon->ibss_mgr_status != 0;

  agg_status = iwl_mvm_get_agg_status(mvm, beacon_notify_hdr);
  status = le16_to_cpu(agg_status->status) & TX_STATUS_MSK;
  IWL_DEBUG_RX(mvm, "beacon status %#x retries:%d tsf:0x%016llX gp2:0x%X rate:%d\n", status,
               beacon_notify_hdr->failure_frame, le64_to_cpu(beacon->tsf), mvm->ap_last_beacon_gp2,
               le32_to_cpu(beacon_notify_hdr->initial_rate));

  csa_vif = rcu_dereference_protected(mvm->csa_vif, lockdep_is_held(&mvm->mutex));
  if (unlikely(csa_vif && csa_vif->csa_active))
    iwl_mvm_csa_count_down(mvm, csa_vif, mvm->ap_last_beacon_gp2, (status == TX_STATUS_SUCCESS));

  tx_blocked_vif = rcu_dereference_protected(mvm->csa_tx_blocked_vif, lockdep_is_held(&mvm->mutex));
  if (unlikely(tx_blocked_vif)) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(tx_blocked_vif);

    /*
     * The channel switch is started and we have blocked the
     * stations. If this is the first beacon (the timeout wasn't
     * set), set the unblock timeout, otherwise countdown
     */
    if (!mvm->csa_tx_block_bcn_timeout) {
      mvm->csa_tx_block_bcn_timeout = IWL_MVM_CS_UNBLOCK_TX_TIMEOUT;
    } else {
      mvm->csa_tx_block_bcn_timeout--;
    }

    /* Check if the timeout is expired, and unblock tx */
    if (mvm->csa_tx_block_bcn_timeout == 0) {
      iwl_mvm_modify_all_sta_disable_tx(mvm, mvmvif, false);
      RCU_INIT_POINTER(mvm->csa_tx_blocked_vif, NULL);
    }
  }
}

static void iwl_mvm_beacon_loss_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_missed_beacons_notif* missed_beacons = _data;
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_mvm* mvm = mvmvif->mvm;
  struct iwl_fw_dbg_trigger_missed_bcon* bcon_trig;
  struct iwl_fw_dbg_trigger_tlv* trigger;
  uint32_t stop_trig_missed_bcon, stop_trig_missed_bcon_since_rx;
  uint32_t rx_missed_bcon, rx_missed_bcon_since_rx;

  if (mvmvif->id != (uint16_t)le32_to_cpu(missed_beacons->mac_id)) {
    return;
  }

  rx_missed_bcon = le32_to_cpu(missed_beacons->consec_missed_beacons);
  rx_missed_bcon_since_rx = le32_to_cpu(missed_beacons->consec_missed_beacons_since_last_rx);
  /*
   * TODO: the threshold should be adjusted based on latency conditions,
   * and/or in case of a CS flow on one of the other AP vifs.
   */
  if (le32_to_cpu(missed_beacons->consec_missed_beacons_since_last_rx) >
      IWL_MVM_MISSED_BEACONS_THRESHOLD) {
    ieee80211_beacon_loss(vif);
  }

  trigger =
      iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif), FW_DBG_TRIGGER_MISSED_BEACONS);
  if (!trigger) {
    return;
  }

  bcon_trig = (void*)trigger->data;
  stop_trig_missed_bcon = le32_to_cpu(bcon_trig->stop_consec_missed_bcon);
  stop_trig_missed_bcon_since_rx = le32_to_cpu(bcon_trig->stop_consec_missed_bcon_since_rx);

  /* TODO: implement start trigger */

  if (rx_missed_bcon_since_rx >= stop_trig_missed_bcon_since_rx ||
      rx_missed_bcon >= stop_trig_missed_bcon) {
    iwl_fw_dbg_collect_trig(&mvm->fwrt, trigger, NULL);
  }
}

void iwl_mvm_rx_missed_beacons_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  struct iwl_missed_beacons_notif* mb = (void*)pkt->data;

  IWL_DEBUG_INFO(mvm, "missed bcn mac_id=%u, consecutive=%u (%u, %u, %u)\n",
                 le32_to_cpu(mb->mac_id), le32_to_cpu(mb->consec_missed_beacons),
                 le32_to_cpu(mb->consec_missed_beacons_since_last_rx),
                 le32_to_cpu(mb->num_recvd_beacons), le32_to_cpu(mb->num_expected_beacons));

  ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                             iwl_mvm_beacon_loss_iterator, mb);

  iwl_fw_dbg_apply_point(&mvm->fwrt, IWL_FW_INI_APPLY_MISSED_BEACONS);
}

void iwl_mvm_rx_stored_beacon_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  struct iwl_stored_beacon_notif* sb = (void*)pkt->data;
  struct ieee80211_rx_status rx_status;
  struct sk_buff* skb;
  uint32_t size = le32_to_cpu(sb->byte_count);

  if (size == 0) {
    return;
  }

  skb = alloc_skb(size, GFP_ATOMIC);
  if (!skb) {
    IWL_ERR(mvm, "alloc_skb failed\n");
    return;
  }

  /* update rx_status according to the notification's metadata */
  memset(&rx_status, 0, sizeof(rx_status));
  rx_status.mactime = le64_to_cpu(sb->tsf);
  /* TSF as indicated by the firmware  is at INA time */
  rx_status.flag |= RX_FLAG_MACTIME_PLCP_START;
  rx_status.device_timestamp = le32_to_cpu(sb->system_time);
  rx_status.band =
      (sb->band & cpu_to_le16(RX_RES_PHY_FLAGS_BAND_24)) ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
  rx_status.freq = ieee80211_channel_to_frequency(le16_to_cpu(sb->channel), rx_status.band);

  /* copy the data */
  skb_put_data(skb, sb->data, size);
  memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));

  /* pass it as regular rx to mac80211 */
  ieee80211_rx_napi(mvm->hw, NULL, skb, NULL);
}

static void iwl_mvm_probe_resp_data_iter(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_probe_resp_data_notif* notif = _data;
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_probe_resp_data *old_data, *new_data;

  if (mvmvif->id != (uint16_t)le32_to_cpu(notif->mac_id)) {
    return;
  }

  new_data = kzalloc(sizeof(*new_data), GFP_KERNEL);
  if (!new_data) {
    return;
  }

  memcpy(&new_data->notif, notif, sizeof(new_data->notif));

  /* noa_attr contains 1 reserved byte, need to substruct it */
  new_data->noa_len = sizeof(struct ieee80211_vendor_ie) + sizeof(new_data->notif.noa_attr) - 1;

  /*
   * If it's a one time NoA, only one descriptor is needed,
   * adjust the length according to len_low.
   */
  if (new_data->notif.noa_attr.len_low == sizeof(struct ieee80211_p2p_noa_desc) + 2) {
    new_data->noa_len -= sizeof(struct ieee80211_p2p_noa_desc);
  }

  old_data =
      rcu_dereference_protected(mvmvif->probe_resp_data, lockdep_is_held(&mvmvif->mvm->mutex));
  rcu_assign_pointer(mvmvif->probe_resp_data, new_data);

  if (old_data) {
    kfree_rcu(old_data, rcu_head);
  }

  if (notif->csa_counter != IWL_PROBE_RESP_DATA_NO_CSA && notif->csa_counter >= 1) {
    ieee80211_csa_set_counter(vif, notif->csa_counter);
  }
}

void iwl_mvm_probe_resp_data_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  struct iwl_probe_resp_data_notif* notif = (void*)pkt->data;
  int len = iwl_rx_packet_payload_len(pkt);

  if (WARN_ON_ONCE(len < sizeof(*notif))) {
    return;
  }

  IWL_DEBUG_INFO(mvm, "Probe response data notif: noa %d, csa %d\n", notif->noa_active,
                 notif->csa_counter);

  ieee80211_iterate_active_interfaces(mvm->hw, IEEE80211_IFACE_ITER_ACTIVE,
                                      iwl_mvm_probe_resp_data_iter, notif);
}

void iwl_mvm_channel_switch_noa_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  struct iwl_channel_switch_noa_notif* notif = (void*)pkt->data;
  struct ieee80211_vif* csa_vif;
  struct iwl_mvm_vif* mvmvif;
  int len = iwl_rx_packet_payload_len(pkt);
  uint32_t id_n_color;

  if (WARN_ON_ONCE(len < sizeof(*notif))) {
    return;
  }

  rcu_read_lock();

  csa_vif = rcu_dereference(mvm->csa_vif);
  if (WARN_ON(!csa_vif || !csa_vif->csa_active)) {
    goto out_unlock;
  }

  id_n_color = le32_to_cpu(notif->id_and_color);

  mvmvif = iwl_mvm_vif_from_mac80211(csa_vif);
  if (WARN(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color) != id_n_color,
           "channel switch noa notification on unexpected vif (csa_vif=%d, notif=%d)",
           FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color), id_n_color)) {
    goto out_unlock;
  }

  IWL_DEBUG_INFO(mvm, "Channel Switch Started Notification\n");

  schedule_delayed_work(&mvm->cs_tx_unblock_dwork, msecs_to_jiffies(IWL_MVM_CS_UNBLOCK_TX_TIMEOUT *
                                                                    csa_vif->bss_conf.beacon_int));

  ieee80211_csa_finish(csa_vif);

  rcu_read_unlock();

  RCU_INIT_POINTER(mvm->csa_vif, NULL);

  return;

out_unlock:
  rcu_read_unlock();
}
#endif  // NEEDS_PORTING
