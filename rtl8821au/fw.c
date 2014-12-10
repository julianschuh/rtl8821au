#include "fw.h"

#define CONFIG_H2C_EF

#define RTL8812_MAX_H2C_BOX_NUMS	4
#define RTL8812_MAX_CMD_LEN	7
#define RTL8812_MESSAGE_BOX_SIZE		4
#define RTL8812_EX_MESSAGE_BOX_SIZE	4


static BOOLEAN Get_RA_ShortGI(struct rtl_priv *Adapter, struct sta_info	*psta,
	uint8_t	shortGIrate)
{
	BOOLEAN		bShortGI;
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	bShortGI = shortGIrate;

#ifdef CONFIG_80211AC_VHT
	if (bShortGI && IsSupportedVHT(psta->wireless_mode)
	 && (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP)
	 && TEST_FLAG(psta->vhtpriv.ldpc_cap, LDPC_VHT_ENABLE_TX)) {
		if (psta->vhtpriv.vht_highest_rate >= MGN_VHT2SS_MCS8)
			bShortGI = _FALSE;
	}
#endif

	return bShortGI;
}

static uint8_t _is_fw_read_cmd_down(struct rtl_priv *padapter, uint8_t msgbox_num)
{
	uint8_t	read_down = _FALSE;
	int 	retry_cnts = 100;

	uint8_t valid;

	/* DBG_8192C(" _is_fw_read_cmd_down ,reg_1cc(%x),msg_box(%d)...\n",rtl_read_byte(padapter,REG_HMETFR),msgbox_num); */

	do {
		valid = rtl_read_byte(padapter, REG_HMETFR) & BIT(msgbox_num);
		if (0 == valid) {
			read_down = _TRUE;
		}
	} while ((!read_down) && (retry_cnts--));

	return read_down;
}




/*****************************************
* H2C Msg format :
* 0x1DF - 0x1D0
*| 31 - 8	| 7-5 	 4 - 0	|
*| h2c_msg 	|Class_ID CMD_ID	|
*
* Extend 0x1FF - 0x1F0
*|31 - 0	  |
*|ext_msg|
******************************************/
static int32_t FillH2CCmd_8812(struct rtl_priv *padapter, uint8_t ElementID, uint32_t CmdLen, uint8_t *pCmdBuffer)
{
	uint8_t bcmd_down = _FALSE;
	int32_t retry_cnts = 100;
	uint8_t h2c_box_num;
	uint32_t msgbox_addr;
	uint32_t msgbox_ex_addr;
	struct rtw_hal *pHalData = GET_HAL_DATA(padapter);
	uint8_t cmd_idx, ext_cmd_len;
	uint32_t h2c_cmd = 0;
	uint32_t h2c_cmd_ex = 0;
	int32_t ret = _FAIL;

	pHalData = GET_HAL_DATA(padapter);


	if (padapter->bFWReady == _FALSE) {
		/* DBG_8192C("FillH2CCmd_8812(): return H2C cmd because fw is not ready\n"); */
		return ret;
	}

	mutex_lock_interruptible(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex));

	if (!pCmdBuffer) {
		goto exit;
	}
	if (CmdLen > RTL8812_MAX_CMD_LEN) {
		goto exit;
	}

	if (padapter->bSurpriseRemoved == _TRUE)
		goto exit;

	/* pay attention to if  race condition happened in  H2C cmd setting. */
	do {
		h2c_box_num = pHalData->LastHMEBoxNum;

		if (!_is_fw_read_cmd_down(padapter, h2c_box_num)) {
			DBG_8192C(" fw read cmd failed...\n");
			goto exit;
		}

		*(uint8_t *)(&h2c_cmd) = ElementID;

		if (CmdLen <= 3) {
			memcpy((uint8_t *)(&h2c_cmd)+1, pCmdBuffer, CmdLen);
		} else {
			memcpy((uint8_t *)(&h2c_cmd)+1, pCmdBuffer, 3);
			ext_cmd_len = CmdLen-3;
			memcpy((uint8_t *)(&h2c_cmd_ex), pCmdBuffer+3, ext_cmd_len);

			/* Write Ext command */
			msgbox_ex_addr = REG_HMEBOX_EXT0_8812 + (h2c_box_num * RTL8812_EX_MESSAGE_BOX_SIZE);
#ifdef CONFIG_H2C_EF
			for (cmd_idx = 0; cmd_idx < ext_cmd_len; cmd_idx++) {
				rtl_write_byte(padapter, msgbox_ex_addr+cmd_idx, *((uint8_t *)(&h2c_cmd_ex)+cmd_idx));
			}
#else
			h2c_cmd_ex = le32_to_cpu(h2c_cmd_ex);
			rtl_write_dword(padapter, msgbox_ex_addr, h2c_cmd_ex);
#endif
		}
		/* Write command */
		msgbox_addr = REG_HMEBOX_0 + (h2c_box_num * RTL8812_MESSAGE_BOX_SIZE);
#ifdef CONFIG_H2C_EF
		for (cmd_idx = 0; cmd_idx < RTL8812_MESSAGE_BOX_SIZE; cmd_idx++) {
			rtl_write_byte(padapter, msgbox_addr+cmd_idx, *((uint8_t *)(&h2c_cmd)+cmd_idx));
		}
#else
		h2c_cmd = le32_to_cpu(h2c_cmd);
		rtl_write_dword(padapter, msgbox_addr, h2c_cmd);
#endif

		bcmd_down = _TRUE;

	/*
	 * 	DBG_8192C("MSG_BOX:%d,CmdLen(%d), reg:0x%x =>h2c_cmd:0x%x, reg:0x%x =>h2c_cmd_ex:0x%x ..\n"
	 * 	 	,pHalData->LastHMEBoxNum ,CmdLen,msgbox_addr,h2c_cmd,msgbox_ex_addr,h2c_cmd_ex);
	 */

		pHalData->LastHMEBoxNum = (h2c_box_num+1) % RTL8812_MAX_H2C_BOX_NUMS;

	} while ((!bcmd_down) && (retry_cnts--));

	ret = _SUCCESS;

exit:

	mutex_unlock(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex));

	return ret;
}

uint8_t rtl8812_h2c_msg_hdl(struct rtl_priv *padapter, unsigned char *pbuf)
{
	uint8_t ElementID, CmdLen;
	uint8_t *pCmdBuffer;
	struct cmd_msg_parm  *pcmdmsg;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	pcmdmsg = (struct cmd_msg_parm *)pbuf;
	ElementID = pcmdmsg->eid;
	CmdLen = pcmdmsg->sz;
	pCmdBuffer = pcmdmsg->buf;

	FillH2CCmd_8812(padapter, ElementID, CmdLen, pCmdBuffer);

	return H2C_SUCCESS;
}

uint8_t rtl8812_set_rssi_cmd(struct rtl_priv *padapter, uint8_t *param)
{
	uint8_t	res = _SUCCESS;
	 struct rtw_hal	*pHalData = GET_HAL_DATA(padapter);

	*((u32 *) param) = cpu_to_le32(*((u32 *) param));

	FillH2CCmd_8812(padapter, H2C_8812_RSSI_REPORT, 4, param);


	return res;
}

uint8_t	Get_VHT_ENI(uint32_t IOTAction, uint32_t WirelessMode, uint32_t	ratr_bitmap)
{
	uint8_t	Ret = 0;

	/* ULLI huh ?? 2.4GHz AC wireless ?? */

	if (WirelessMode == WIRELESS_11_24AC) {
		if (ratr_bitmap & 0xfff00000)	/* Mix , 2SS */
			Ret = 3;
		else 				/* Mix, 1SS */
			Ret = 2;
	} else if (WirelessMode == WIRELESS_11_5AC) {
		Ret = 1;			/* VHT */
	}

	return (Ret << 4);
}


void Set_RA_LDPC_8812(struct sta_info *psta, BOOLEAN bLDPC)
{
	if (psta == NULL)
		return;

#ifdef CONFIG_80211AC_VHT
	if (psta->wireless_mode == WIRELESS_11_5AC) {
		if (bLDPC && TEST_FLAG(psta->vhtpriv.ldpc_cap, LDPC_VHT_CAP_TX))
			SET_FLAG(psta->vhtpriv.ldpc_cap, LDPC_VHT_ENABLE_TX);
		else
			CLEAR_FLAG(psta->vhtpriv.ldpc_cap, LDPC_VHT_ENABLE_TX);
	} else
		if (IsSupportedTxHT(psta->wireless_mode) || IsSupportedVHT(psta->wireless_mode)) {
			if (bLDPC && TEST_FLAG(psta->htpriv.ldpc_cap, LDPC_HT_CAP_TX))
				SET_FLAG(psta->htpriv.ldpc_cap, LDPC_HT_ENABLE_TX);
			else
				CLEAR_FLAG(psta->htpriv.ldpc_cap, LDPC_HT_ENABLE_TX);
		}
#endif

	/* DBG_871X("MacId %d bLDPC %d\n", psta->mac_id, bLDPC); */
}

u8 Get_RA_LDPC_8812(struct sta_info *psta)
{
	uint8_t	bLDPC = 0;

	if (psta->mac_id == 1)
		bLDPC = 0;
	else
		if (psta != NULL) {
#ifdef CONFIG_80211AC_VHT
			if (IsSupportedVHT(psta->wireless_mode)) {
				if (TEST_FLAG(psta->vhtpriv.ldpc_cap, LDPC_VHT_CAP_TX))
					bLDPC = 1;
				else
					bLDPC = 0;
			} else
				if (IsSupportedTxHT(psta->wireless_mode)) {
					if (TEST_FLAG(psta->htpriv.ldpc_cap, LDPC_HT_CAP_TX))
						bLDPC = 1;
					else
						bLDPC = 0;
				} else
#endif
				bLDPC = 0;
	}

	return (bLDPC << 2);
}



void rtl8812_set_raid_cmd(struct rtl_priv *padapter, uint32_t bitmap, uint8_t *arg)
{
	struct rtw_hal	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info	*psta;
	uint8_t macid, init_rate, raid, shortGIrate = _FALSE;

	macid = arg[0];
	raid = arg[1];
	shortGIrate = arg[2];
	init_rate = arg[3];

	psta = pmlmeinfo->FW_sta_info[macid].psta;
	if (psta == NULL) {
		return;
	}

	if (pHalData->fw_ractrl == _TRUE) {
		uint8_t	H2CCommand[7] = {0};

		shortGIrate = Get_RA_ShortGI(padapter, psta, shortGIrate);

		H2CCommand[0] = macid;
		H2CCommand[1] = (raid & 0x1F) | (shortGIrate?0x80:0x00) ;
		H2CCommand[2] = (pmlmeext->cur_bwmode & 0x3) | Get_RA_LDPC_8812(psta) | Get_VHT_ENI(0, psta->wireless_mode, bitmap);

		H2CCommand[3] = (uint8_t)(bitmap & 0x000000ff);
		H2CCommand[4] = (uint8_t)((bitmap & 0x0000ff00) >> 8);
		H2CCommand[5] = (uint8_t)((bitmap & 0x00ff0000) >> 16);
		H2CCommand[6] = (uint8_t)((bitmap & 0xff000000) >> 24);

		DBG_871X("rtl8812_set_raid_cmd, bitmap=0x%x, mac_id=0x%x, raid=0x%x, shortGIrate=%x\n", bitmap, macid, raid, shortGIrate);

		FillH2CCmd_8812(padapter, H2C_8812_RA_MASK, 7, H2CCommand);
	}

	if (shortGIrate == _TRUE)
		init_rate |= BIT(7);

	pdmpriv->INIDATA_RATE[macid] = init_rate;

}


/*
 * Description: Get the reserved page number in Tx packet buffer.
 * Retrun value: the page number.
 * 2012.08.09, by tynli.
 */
u8 GetTxBufferRsvdPageNum8812(struct rtl_priv *Adapter, BOOLEAN	bWoWLANBoundary)
{
	 struct rtw_hal	*pHalData = GET_HAL_DATA(Adapter);
	uint8_t	RsvdPageNum = 0;
	uint8_t	TxPageBndy = LAST_ENTRY_OF_TX_PKT_BUFFER_8812; /* default reseved 1 page for the IC type which is undefined. */

	if (bWoWLANBoundary) {
		rtw_hal_get_def_var(Adapter, HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN, (uint8_t *)&TxPageBndy);
	} else {
		rtw_hal_get_def_var(Adapter, HAL_DEF_TX_PAGE_BOUNDARY, (uint8_t *)&TxPageBndy);
	}

	RsvdPageNum = LAST_ENTRY_OF_TX_PKT_BUFFER_8812 - TxPageBndy + 1;

	return RsvdPageNum;
}

void ConstructBeacon(struct rtl_priv *padapter, uint8_t *pframe, uint32_t *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16	*fctrl;
	uint32_t					rate_len, pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	uint8_t	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


	/* DBG_871X("%s\n", __FUNCTION__); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */
	SetFrameSubType(pframe, WIFI_BEACON);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof (struct rtw_ieee80211_hdr_3addr);

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pktlen += 8;

	/* beacon interval: 2 bytes */
	memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	/* capability info: 2 bytes */
	memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
		/* DBG_871X("ie len=%d\n", cur_network->IELength); */
		pktlen += cur_network->IELength - sizeof(NDIS_802_11_FIXED_IEs);
		memcpy(pframe, cur_network->IEs+sizeof(NDIS_802_11_FIXED_IEs), pktlen);

		goto _ConstructBeacon;
	}

	/* below for ad-hoc mode */

	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	/* supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pktlen);

	/* DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
		uint32_t ATIMWindow;
		/*
		 * IBSS Parameter Set...
		 * ATIMWindow = cur->Configuration.ATIMWindow;
		 */
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pktlen);
	}


	/* todo: ERP IE */


	/* EXTERNDED SUPPORTED RATE */
	if (rate_len > 8) {
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);
	}


	/* todo:HT for adhoc */

_ConstructBeacon:

	if ((pktlen + TXDESC_SIZE) > 512) {
		DBG_871X("beacon frame too large\n");
		return;
	}

	*pLength = pktlen;

	/* DBG_871X("%s bcn_sz=%d\n", __FUNCTION__, pktlen); */

}

void ConstructPSPoll(struct rtl_priv *padapter, uint8_t *pframe, uint32_t *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	uint32_t					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	/* DBG_871X("%s\n", __FUNCTION__); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	/* Frame control. */
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	SetPwrMgt(fctrl);
	SetFrameSubType(pframe, WIFI_PSPOLL);

	/* AID. */
	SetDuration(pframe, (pmlmeinfo->aid | 0xc000));

	/* BSSID. */
	memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	/* TA. */
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);

	*pLength = 16;
}

void ConstructNullFunctionData(
	struct rtl_priv *padapter,
	uint8_t		*pframe,
	uint32_t		*pLength,
	uint8_t		*StaAddr,
	uint8_t		bQoS,
	uint8_t		AC,
	uint8_t		bEosp,
	uint8_t		bForcePowerSave)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16						*fctrl;
	uint32_t						pktlen;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network		*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	/* DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;
	if (bForcePowerSave) {
		SetPwrMgt(fctrl);
	}

	switch (cur_network->network.InfrastructureMode) {
	case Ndis802_11Infrastructure:
		SetToDs(fctrl);
		memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
		memcpy(pwlanhdr->addr3, StaAddr, ETH_ALEN);
		break;
	case Ndis802_11APMode:
		SetFrDs(fctrl);
		memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		memcpy(pwlanhdr->addr2, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);
		break;
	case Ndis802_11IBSS:
	default:
		memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
		memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		break;
	}

	SetSeqNum(pwlanhdr, 0);

	if (bQoS == _TRUE) {
		struct rtw_ieee80211_hdr_3addr_qos *pwlanqoshdr;

		SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

		pwlanqoshdr = (struct rtw_ieee80211_hdr_3addr_qos *) pframe;
		SetPriority(&pwlanqoshdr->qc, AC);
		SetEOSP(&pwlanqoshdr->qc, bEosp);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos);
	} else {
		SetFrameSubType(pframe, WIFI_DATA_NULL);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	}

	*pLength = pktlen;
}


/*
 * Description: Fill the reserved packets that FW will use to RSVD page.
 * 			Now we just send 4 types packet to rsvd page.
 * 			(1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp.
 * 	Input:
 * 	    bDLFinished - FALSE: At the first time we will send all the packets as a large packet to Hw,
 * 				 		so we need to set the packet length to total lengh.
 * 			      TRUE: At the second time, we should send the first packet (default:beacon)
 * 						to Hw again and set the lengh in descriptor to the real beacon lengh.
 *  2009.10.15 by tynli.
 */
static void SetFwRsvdPagePkt_8812(struct rtl_priv *padapter, BOOLEAN bDLFinished)
{
	struct rtw_hal *pHalData;
	struct xmit_frame	*pcmdframe;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	uint32_t	PSPollLength, NullFunctionDataLength, QosNullLength;
	uint32_t	BcnLen;
	uint8_t	TotalPageNum = 0, CurtPktPageNum = 0, TxDescLen = 0, RsvdPageNum = 0;
	uint8_t	*ReservedPagePacket;
	uint8_t	RsvdPageLoc[5] = {0};
	u16	BufIndex = 0, PageSize = 256;
	uint32_t TotalPacketLen, MaxRsvdPageBufSize = 0;;


	/* DBG_871X("%s\n", __FUNCTION__); */

	pHalData = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	if (IS_HARDWARE_TYPE_8812(padapter))
		PageSize = 512;
	else if (IS_HARDWARE_TYPE_8821(padapter))
		PageSize = PAGE_SIZE_TX_8821A;

	/*
	 *  <tynli_note> The function SetFwRsvdPagePkt_8812() input must be added a value "bDLWholePackets" to
	 *  decide if download wowlan packets, and use "bDLWholePackets" to be GetTxBufferRsvdPageNum8812() 2nd input value.
	 */
	RsvdPageNum = GetTxBufferRsvdPageNum8812(padapter, _FALSE);
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv, MaxRsvdPageBufSize);
	if (pcmdframe == NULL) {
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;

	TxDescLen = TXDESC_SIZE;	/* The desc lengh in Tx packet buffer of 8812A is 40 bytes. */

	/* (1) beacon */
	BufIndex = TXDESC_OFFSET;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BcnLen);

	/*
	 *  When we count the first page size, we need to reserve description size for the RSVD
	 *  packet, it will be filled in front of the packet in TXPKTBUF.
	 */
	CurtPktPageNum = (uint8_t)PageNum(BcnLen+TxDescLen, PageSize);

	if (bDLFinished) {
		TotalPageNum += CurtPktPageNum;
		TotalPacketLen = (TotalPageNum*PageSize);
		DBG_871X("%s(): Beacon page size = %d\n", __FUNCTION__, TotalPageNum);
	} else {
		TotalPageNum += CurtPktPageNum;

		pHalData->FwRsvdPageStartOffset = TotalPageNum;

		BufIndex += (CurtPktPageNum*PageSize);

		if (BufIndex > MaxRsvdPageBufSize) {
			DBG_871X("%s(): Beacon: The rsvd page size is not enough!!BufIndex %d, MaxRsvdPageBufSize %d\n", __FUNCTION__,
				BufIndex, MaxRsvdPageBufSize);
			goto error;
		}

		/* (2) ps-poll */
		ConstructPSPoll(padapter, &ReservedPagePacket[BufIndex], &PSPollLength);
		rtw_hal_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], PSPollLength, _TRUE, _FALSE);

		SET_8812_H2CCMD_RSVDPAGE_LOC_PSPOLL(RsvdPageLoc, TotalPageNum);

		/*
		 * DBG_871X("SetFwRsvdPagePkt_8812(): HW_VAR_SET_TX_CMD: PS-POLL %p %d\n",
		 * 	&ReservedPagePacket[BufIndex-TxDescLen], (PSPollLength+TxDescLen));
		 */

		CurtPktPageNum = (uint8_t)PageNum(PSPollLength+TxDescLen, PageSize);

		BufIndex += (CurtPktPageNum*PageSize);

		TotalPageNum += CurtPktPageNum;

		if (BufIndex > MaxRsvdPageBufSize) {
			DBG_871X("%s(): ps-poll: The rsvd page size is not enough!!BufIndex %d, MaxRsvdPageBufSize %d\n", __FUNCTION__,
				BufIndex, MaxRsvdPageBufSize);
			goto error;
		}

		/* (3) null data */
		ConstructNullFunctionData(
			padapter,
			&ReservedPagePacket[BufIndex],
			&NullFunctionDataLength,
			get_my_bssid(&pmlmeinfo->network),
			_FALSE, 0, 0, _FALSE);
		rtw_hal_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], NullFunctionDataLength, _FALSE, _FALSE);

		SET_8812_H2CCMD_RSVDPAGE_LOC_NULL_DATA(RsvdPageLoc, TotalPageNum);

		/*
		 * DBG_871X("SetFwRsvdPagePkt_8812(): HW_VAR_SET_TX_CMD: NULL DATA %p %d\n",
		 * 	&ReservedPagePacket[BufIndex-TxDescLen], (NullFunctionDataLength+TxDescLen));
		 */

		CurtPktPageNum = (uint8_t)PageNum(NullFunctionDataLength+TxDescLen, PageSize);

		BufIndex += (CurtPktPageNum*PageSize);

		TotalPageNum += CurtPktPageNum;

		if (BufIndex > MaxRsvdPageBufSize) {
			DBG_871X("%s(): Null-data: The rsvd page size is not enough!!BufIndex %d, MaxRsvdPageBufSize %d\n", __FUNCTION__,
				BufIndex, MaxRsvdPageBufSize);
			goto error;
		}

		/* (5) Qos null data */
		ConstructNullFunctionData(
			padapter,
			&ReservedPagePacket[BufIndex],
			&QosNullLength,
			get_my_bssid(&pmlmeinfo->network),
			_TRUE, 0, 0, _FALSE);
		rtw_hal_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], QosNullLength, _FALSE, _FALSE);

		SET_8812_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(RsvdPageLoc, TotalPageNum);

		/*
		 * DBG_871X("SetFwRsvdPagePkt_8812(): HW_VAR_SET_TX_CMD: QOS NULL DATA %p %d\n",
		 * 	&ReservedPagePacket[BufIndex-TxDescLen], (QosNullLength+TxDescLen));
		 */

		CurtPktPageNum = (uint8_t)PageNum(QosNullLength+TxDescLen, PageSize);

		BufIndex += (CurtPktPageNum*PageSize);

		TotalPageNum += CurtPktPageNum;

		TotalPacketLen = (TotalPageNum * PageSize);
	}


	if (TotalPacketLen > MaxRsvdPageBufSize) {
		DBG_871X("%s(): ERROR: The rsvd page size is not enough!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n", __FUNCTION__,
			TotalPacketLen, MaxRsvdPageBufSize);
		goto error;
	} else {
		/* update attribute */
		pattrib = &pcmdframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);
		pattrib->qsel = 0x10;
		pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TxDescLen;

		dump_mgntframe_and_wait(padapter, pcmdframe, 100);
	}

	if (!bDLFinished) {
		DBG_871X("%s: Set RSVD page location to Fw ,TotalPacketLen(%d), TotalPageNum(%d)\n", __FUNCTION__, TotalPacketLen, TotalPageNum);
		FillH2CCmd_8812(padapter, H2C_8812_RSVDPAGE, 5, RsvdPageLoc);
	}

	rtw_free_cmd_xmitbuf(pxmitpriv);

	return;

error:
	rtw_free_cmdxmitframe(pxmitpriv, pcmdframe);
}

void rtl8812_set_FwJoinBssReport_cmd(struct rtl_priv *padapter, uint8_t mstatus)
{
	 struct rtw_hal	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	BOOLEAN		bSendBeacon = _FALSE;
	BOOLEAN		bcn_valid = _FALSE;
	uint8_t	DLBcnCount = 0;
	uint32_t poll = 0;

	DBG_871X("%s mstatus(%x)\n", __FUNCTION__, mstatus);

	if (mstatus == 1) {
		/*
		 *  We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C.
		 *  Suggested by filen. Added by tynli.
		 */
		rtl_write_word(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));
		/*
		 *  Do not set TSF again here or vWiFi beacon DMA INT will not work.
		 *  correct_TSF(padapter, pmlmeext);
		 *  Hw sequende enable by dedault. 2010.06.23. by tynli.
		 * rtl_write_word(padapter, REG_NQOS_SEQ, ((pmlmeext->mgnt_seq+100)&0xFFF));
		 * rtl_write_byte(padapter, REG_HWSEQ_CTRL, 0xFF);
		 */

		/* Set REG_CR bit 8. DMA beacon by SW. */
		pHalData->RegCR_1 |= BIT0;
		rtl_write_byte(padapter,  REG_CR+1, pHalData->RegCR_1);

		/*
		 * Disable Hw protection for a time which revserd for Hw sending beacon.
		 * Fix download reserved page packet fail that access collision with the protection time.
		 * 2010.05.11. Added by tynli.
		 * SetBcnCtrlReg(padapter, 0, BIT3);
		 * SetBcnCtrlReg(padapter, BIT4, 0);
		 */
		rtl_write_byte(padapter, REG_BCN_CTRL, rtl_read_byte(padapter, REG_BCN_CTRL)&(~BIT(3)));
		rtl_write_byte(padapter, REG_BCN_CTRL, rtl_read_byte(padapter, REG_BCN_CTRL)|BIT(4));

		if (pHalData->RegFwHwTxQCtrl&BIT6) {
			DBG_871X("HalDownloadRSVDPage(): There is an Adapter is sending beacon.\n");
			bSendBeacon = _TRUE;
		}

		/* Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame. */
		rtl_write_byte(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl&(~BIT6)));
		pHalData->RegFwHwTxQCtrl &= (~BIT6);

		/* Clear beacon valid check bit. */
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
		DLBcnCount = 0;
		poll = 0;

		do {
			/* download rsvd page. */
			SetFwRsvdPagePkt_8812(padapter, _FALSE);
			DLBcnCount++;
			do {
				rtw_yield_os();
				/*
				 * rtw_mdelay_os(10);
				 * check rsvd page download OK.
				 */
				rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (uint8_t *)(&bcn_valid));
				poll++;
			} while (!bcn_valid && (poll%10) != 0 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

		} while (!bcn_valid && DLBcnCount <= 100 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

		/* RT_ASSERT(bcn_valid, ("HalDownloadRSVDPage88ES(): 1 Download RSVD page failed!\n")); */
		if (padapter->bSurpriseRemoved || padapter->bDriverStopped) {
		} else if (!bcn_valid)
			DBG_871X("%s: 1 Download RSVD page failed! DLBcnCount:%u, poll:%u\n", __FUNCTION__ , DLBcnCount, poll);
		else
			DBG_871X("%s: 1 Download RSVD success! DLBcnCount:%u, poll:%u\n", __FUNCTION__, DLBcnCount, poll);
		/*
		 * We just can send the reserved page twice during the time that Tx thread is stopped (e.g. pnpsetpower)
		 * becuase we need to free the Tx BCN Desc which is used by the first reserved page packet.
		 * At run time, we cannot get the Tx Desc until it is released in TxHandleInterrupt() so we will return
		 * the beacon TCB in the following code. 2011.11.23. by tynli.
		 */

		/* if (bcn_valid && padapter->bEnterPnpSleep) */
		if (0) {
			if (bSendBeacon) {
				rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
				DLBcnCount = 0;
				poll = 0;
				do {
					SetFwRsvdPagePkt_8812(padapter, _TRUE);
					DLBcnCount++;

					do {
						rtw_yield_os();
						/*
						 * rtw_mdelay_os(10);
						 * check rsvd page download OK.
						 */
						rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (uint8_t *)(&bcn_valid));
						poll++;
					} while (!bcn_valid && (poll%10) != 0 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);
				} while (!bcn_valid && DLBcnCount <= 100 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

				/* RT_ASSERT(bcn_valid, ("HalDownloadRSVDPage(): 2 Download RSVD page failed!\n")); */
				if (padapter->bSurpriseRemoved || padapter->bDriverStopped) {
				} else if (!bcn_valid)
					DBG_871X("%s: 2 Download RSVD page failed! DLBcnCount:%u, poll:%u\n", __FUNCTION__ , DLBcnCount, poll);
				else
					DBG_871X("%s: 2 Download RSVD success! DLBcnCount:%u, poll:%u\n", __FUNCTION__, DLBcnCount, poll);
			}
		}

		/* Enable Bcn */
		/* SetBcnCtrlReg(padapter, BIT3, 0); */
		/* SetBcnCtrlReg(padapter, 0, BIT4); */
		rtl_write_byte(padapter, REG_BCN_CTRL, rtl_read_byte(padapter, REG_BCN_CTRL)|BIT(3));
		rtl_write_byte(padapter, REG_BCN_CTRL, rtl_read_byte(padapter, REG_BCN_CTRL)&(~BIT(4)));

		/*
		 * To make sure that if there exists an adapter which would like to send beacon.
		 * If exists, the origianl value of 0x422[6] will be 1, we should check this to
		 * prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
		 * the beacon cannot be sent by HW.
		 * 2010.06.23. Added by tynli.
		 */

		if (bSendBeacon) {
			rtl_write_byte(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl|BIT6));
			pHalData->RegFwHwTxQCtrl |= BIT6;
		}

		/*
		 * Update RSVD page location H2C to Fw.
		 */

		if (bcn_valid) {
			rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
			DBG_871X("Set RSVD page location to Fw.\n");
			/* FillH2CCmd88E(Adapter, H2C_88E_RSVDPAGE, H2C_RSVDPAGE_LOC_LENGTH, pMgntInfo->u1RsvdPageLoc); */
		}

		/*  Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli. */
		/* if (!padapter->bEnterPnpSleep) */
		{
			/* Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
			pHalData->RegCR_1 &= (~BIT0);
			rtl_write_byte(padapter,  REG_CR+1, pHalData->RegCR_1);
		}
	}

}



void rtl8812_set_FwMediaStatus_cmd(struct rtl_priv *padapter, u16 mstatus_rpt)
{
	uint8_t	u1JoinBssRptParm[3] = {0};
	uint8_t	mstatus, macId, macId_Ind = 0, macId_End = 0;

	mstatus = (uint8_t) (mstatus_rpt & 0xFF);
	macId = (uint8_t)(mstatus_rpt >> 8)  ;

	SET_8812_H2CCMD_MSRRPT_PARM_OPMODE(u1JoinBssRptParm, mstatus);
	SET_8812_H2CCMD_MSRRPT_PARM_MACID_IND(u1JoinBssRptParm, macId_Ind);

	SET_8812_H2CCMD_MSRRPT_PARM_MACID(u1JoinBssRptParm, macId);
	SET_8812_H2CCMD_MSRRPT_PARM_MACID_END(u1JoinBssRptParm, macId_End);

	DBG_871X("[MacId],  Set MacId Ctrl(original) = 0x%x \n", u1JoinBssRptParm[0]<<16|u1JoinBssRptParm[1]<<8|u1JoinBssRptParm[2]);

	FillH2CCmd_8812(padapter, H2C_8812_MSRRPT, 3, u1JoinBssRptParm);
}


void rtl8812au_set_fw_pwrmode_cmd(struct rtl_priv *padapter, uint8_t PSMode)
{
	uint8_t	u1H2CSetPwrMode[5] = {0};
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	uint8_t	Mode = 0, RLBM = 0, PowerState = 0, LPSAwakeIntvl = 1;

	DBG_871X("%s: Mode=%d SmartPS=%d UAPSD=%d\n", __FUNCTION__,
			PSMode, pwrpriv->smart_ps, padapter->registrypriv.uapsd_enable);

	switch (PSMode) {
	case PS_MODE_ACTIVE:
		Mode = 0;
		break;
	case PS_MODE_MIN:
		Mode = 1;
		break;
	case PS_MODE_MAX:
		RLBM = 1;
		Mode = 1;
		break;
	case PS_MODE_DTIM:
		RLBM = 2;
		Mode = 1;
		break;
	case PS_MODE_UAPSD_WMM:
		Mode = 2;
		break;
	default:
		Mode = 0;
		break;
	}

	if (Mode > PS_MODE_ACTIVE) {
		PowerState = 0x00;	/* AllON(0x0C), RFON(0x04), RFOFF(0x00) */
	} else {
		PowerState = 0x0C;	/* AllON(0x0C), RFON(0x04), RFOFF(0x00) */
	}

	/* 0: Active, 1: LPS, 2: WMMPS */
	SET_8812_H2CCMD_PWRMODE_PARM_MODE(u1H2CSetPwrMode, Mode);

	/* 0:Min, 1:Max , 2:User define */
	SET_8812_H2CCMD_PWRMODE_PARM_RLBM(u1H2CSetPwrMode, RLBM);

	/* (LPS) smart_ps:  0: PS_Poll, 1: PS_Poll , 2: NullData */
	/* (WMM)smart_ps: 0:PS_Poll, 1:NullData */
	SET_8812_H2CCMD_PWRMODE_PARM_SMART_PS(u1H2CSetPwrMode, pwrpriv->smart_ps);

	/* AwakeInterval: Unit is beacon interval, this field is only valid in PS_DTIM mode */
	SET_8812_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1H2CSetPwrMode, LPSAwakeIntvl);

	/* (WMM only)bAllQueueUAPSD */
	SET_8812_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(u1H2CSetPwrMode, padapter->registrypriv.uapsd_enable);

	/* AllON(0x0C), RFON(0x04), RFOFF(0x00) */
	SET_8812_H2CCMD_PWRMODE_PARM_PWR_STATE(u1H2CSetPwrMode, PowerState);

	FillH2CCmd_8812(padapter, H2C_8812_SETPWRMODE, sizeof(u1H2CSetPwrMode), (uint8_t *)&u1H2CSetPwrMode);
}