// -----------------------------------------------------------------------------
// ICQ plugin for Miranda NG
// -----------------------------------------------------------------------------
// Copyright � 2018-19 Miranda NG team
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
// -----------------------------------------------------------------------------
// Long-poll thread and its item handlers

#include "stdafx.h"

void CIcqProto::ProcessBuddyList(const JSONNode &ev)
{
	bool bEnableMenu = false;

	for (auto &it : ev["groups"]) {
		CMStringW szGroup = it["name"].as_mstring();
		bool bCreated = false;

		for (auto &buddy : it["buddies"]) {
			MCONTACT hContact = ParseBuddyInfo(buddy);
			if (hContact == INVALID_CONTACT_ID)
				continue;

			setWString(hContact, "IcqGroup", szGroup);

			CMStringW mirGroup(db_get_sm(hContact, "CList", "Group"));
			if (mirGroup != szGroup)
				bEnableMenu = true;

			if (mirGroup.IsEmpty()) {
				if (!bCreated) {
					Clist_GroupCreate(0, szGroup);
					bCreated = true;
				}

				db_set_ws(hContact, "CList", "Group", szGroup);
			}
		}
	}

	if (bEnableMenu)
		Menu_ShowItem(m_hUploadGroups, true);

	for (auto &it : m_arCache)
		if (!it->m_bInList)
			db_set_b(it->m_hContact, "CList", "NotOnList", 1);

	RetrieveUserInfo();
}

void CIcqProto::ProcessDiff(const JSONNode &ev)
{
	for (auto &block : ev) {
		CMStringW szType = block["type"].as_mstring();
		if (szType != "updated" && szType != "created")
			continue;

		for (auto &it : block["data"]) {
			CMStringW szGroup = it["name"].as_mstring();
			bool bCreated = false;

			for (auto &buddy : it["buddies"]) {
				MCONTACT hContact = ParseBuddyInfo(buddy);
				if (hContact == INVALID_CONTACT_ID)
					continue;

				setWString(hContact, "IcqGroup", szGroup);

				if (db_get_sm(hContact, "CList", "Group").IsEmpty()) {
					if (!bCreated) {
						Clist_GroupCreate(0, szGroup);
						bCreated = true;
					}

					db_set_ws(hContact, "CList", "Group", szGroup);
				}
			}
		}
	}
}

void CIcqProto::ProcessEvent(const JSONNode &ev)
{
	const JSONNode &pData = ev["eventData"];
	CMStringW szType = ev["type"].as_mstring();
	if (szType == L"buddylist")
		ProcessBuddyList(pData);
	else if (szType == L"diff")
		ProcessDiff(pData);
	else if (szType == L"histDlgState")
		ProcessHistData(pData);
	else if (szType == L"imState")
		ProcessImState(pData);
	else if (szType == L"mchat")
		ProcessGroupChat(pData);
	else if (szType == L"myInfo")
		ProcessMyInfo(pData);
	else if (szType == L"notification")
		ProcessNotification(pData);
	else if (szType == L"permitDeny")
		ProcessPermissions(pData);
	else if (szType == L"presence")
		ProcessPresence(pData);
	else if (szType == L"typing")
		ProcessTyping(pData);
}

void CIcqProto::ProcessHistData(const JSONNode &ev)
{
	MCONTACT hContact;

	CMStringW wszId(ev["sn"].as_mstring());
	if (IsChat(wszId)) {
		SESSION_INFO *si = g_chatApi.SM_FindSession(wszId, m_szModuleName);
		if (si == nullptr)
			return;

		hContact = si->hContact;

		if (si->arUsers.getCount() == 0) {
			__int64 srvInfoVer = _wtoi64(ev["mchatState"]["infoVersion"].as_mstring());
			__int64 srvMembersVer = _wtoi64(ev["mchatState"]["membersVersion"].as_mstring());
			if (srvInfoVer != getId(hContact, "InfoVersion") || srvMembersVer != getId(hContact, "MembersVersion")) {
				auto *pReq = new AsyncHttpRequest(CONN_RAPI, REQUEST_POST, ICQ_ROBUST_SERVER, &CIcqProto::OnGetChatInfo);
				JSONNode request, params; params.set_name("params");
				params << WCHAR_PARAM("sn", wszId) << INT_PARAM("memberLimit", 100) << CHAR_PARAM("aimSid", m_aimsid);
				request << CHAR_PARAM("method", "getChatInfo") << CHAR_PARAM("reqId", pReq->m_reqId) << CHAR_PARAM("authToken", m_szRToken) << INT_PARAM("clientId", m_iRClientId) << params;
				pReq->m_szParam = ptrW(json_write(&request));
				pReq->pUserInfo = si;
				Push(pReq);
			}
			else LoadChatInfo(si);
		}
	}
	else hContact = CreateContact(wszId, true);

	__int64 lastMsgId = getId(hContact, DB_KEY_LASTMSGID);
	if (lastMsgId == 0) {
		lastMsgId = _wtoi64(ev["yours"]["lastRead"].as_mstring());
		setId(hContact, DB_KEY_LASTMSGID, lastMsgId);
	}

	// or load missing messages if any
	switch (ev["unreadCnt"].as_int()) {
	case 0: break;
	case 1:
		if (!IsChat(wszId))
			for (auto &it : ev["tail"]["messages"])
				ParseMessage(hContact, lastMsgId, it, false);
		setId(hContact, DB_KEY_LASTMSGID, lastMsgId);
		break;

	default:
		RetrieveUserHistory(hContact, lastMsgId);
	}

	// check remote read
	if (g_bMessageState) {
		__int64 srvRemoteRead = _wtoi64(ev["theirs"]["lastRead"].as_mstring());
		__int64 lastRemoteRead = getId(hContact, DB_KEY_REMOTEREAD);
		if (srvRemoteRead > lastRemoteRead) {
			setId(hContact, DB_KEY_REMOTEREAD, srvRemoteRead);

			MessageReadData data(time(0), MRD_TYPE_READTIME);
			CallService(MS_MESSAGESTATE_UPDATE, hContact, (LPARAM)&data);
		}
	}
}

void CIcqProto::ProcessImState(const JSONNode &ev)
{
	for (auto &it : ev["imStates"]) {
		if (it["state"].as_mstring() != L"sent")
			continue;

		CMStringA reqId(it["sendReqId"].as_mstring());
		CMStringA msgId(it["histMsgId"].as_mstring());
		MCONTACT hContact = CheckOwnMessage(reqId, msgId, false);
		if (hContact)
			CheckLastId(hContact, ev);
	}
}

void CIcqProto::ProcessMyInfo(const JSONNode &ev)
{
	Json2string(0, ev, "friendly", "Nick");
	CheckAvatarChange(0, ev);
}

void CIcqProto::ProcessNotification(const JSONNode &ev)
{
	for (auto &fld : ev["fields"]) {
		const JSONNode &email = fld["mailbox.newMessage"];
		if (email) {
			JSONROOT root(email.as_string().c_str());
			CMStringW wszFrom((*root)["from"].as_mstring());
			CMStringW wszSubj((*root)["subject"].as_mstring());
			m_unreadEmails = (*root)["unreadCount"].as_int();

			POPUPDATAT Popup = {};
			mir_snwprintf(Popup.lptzText, LPGENW("You received e-mail from %s: %s"), wszFrom.c_str(), wszSubj.c_str());
			Popup.lchIcon = IcoLib_GetIconByHandle(iconList[1].hIcolib);
			if (g_bPopupService) {
				wcsncpy_s(Popup.lptzContactName, m_tszUserName, _TRUNCATE);
				CallService(MS_POPUP_ADDPOPUPT, (WPARAM)&Popup, 0);
			}

			char szServiceFunction[MAX_PATH];
			mir_snprintf(szServiceFunction, "%s%s", m_szModuleName, PS_GOTO_INBOX);

			CLISTEVENT cle = {};
			cle.hDbEvent = 1;
			cle.hIcon = Popup.lchIcon;
			cle.flags = CLEF_UNICODE;
			cle.pszService = szServiceFunction;
			cle.szTooltip.w = Popup.lptzText;
			g_clistApi.pfnAddEvent(&cle);
		}

		const JSONNode &status = fld["mailbox.status"];
		if (status) {
			JSONROOT root(status.as_string().c_str());
			m_szMailBox = (*root)["email"].as_mstring();
			m_unreadEmails = (*root)["unreadCount"].as_int();
		}
	}
}

void CIcqProto::ProcessPresence(const JSONNode &ev)
{
	CMStringW aimId = ev["aimId"].as_mstring();

	IcqCacheItem *pCache = FindContactByUIN(aimId);
	if (pCache) {
		int iNewStatus = StatusFromString(ev["state"].as_mstring());

		// major crutch dedicated to the official client behaviour to go offline
		// when its window gets closed. we change the status of a contact to the
		// first chosen one from options and initialize a timer
		if (iNewStatus == ID_STATUS_OFFLINE) {
			if (m_iTimeDiff1) {
				iNewStatus = m_iStatus1;
				pCache->m_timer1 = time(0);
			}
		}
		// if a client returns back online, we clear timers not to play with statuses anymore
		else pCache->m_timer1 = pCache->m_timer2 = 0;

		setDword(pCache->m_hContact, "Status", iNewStatus);

		Json2string(pCache->m_hContact, ev, "friendly", "Nick");
		CheckAvatarChange(pCache->m_hContact, ev);
	}
}

void CIcqProto::ProcessTyping(const JSONNode &ev)
{
	CMStringW aimId = ev["aimId"].as_mstring();
	CMStringW wszStatus = ev["typingStatus"].as_mstring();

	IcqCacheItem *pCache = FindContactByUIN(aimId);
	if (pCache) {
		if (wszStatus == "typing")
			CallService(MS_PROTO_CONTACTISTYPING, pCache->m_hContact, 60);
		else 
			CallService(MS_PROTO_CONTACTISTYPING, pCache->m_hContact, PROTOTYPE_CONTACTTYPING_OFF);
	}
}

void CIcqProto::OnFetchEvents(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest*)
{
	JsonReply root(pReply);
	if (root.error() != 200) {
		ShutdownSession();
		return;
	}

	JSONNode &data = root.data();
	m_fetchBaseURL = data["fetchBaseURL"].as_mstring();

	for (auto &it : data["events"])
		ProcessEvent(it);
}

void __cdecl CIcqProto::PollThread(void*)
{
	debugLogA("Polling thread started");
	m_bFirstBos = true;

	while (m_bOnline) {
		CMStringA szUrl = m_fetchBaseURL;
		if (m_bFirstBos)
			szUrl.Append("&first=1");
		else
			szUrl.Append("&timeout=25000");

		auto *pReq = new AsyncHttpRequest(CONN_FETCH, REQUEST_GET, szUrl, &CIcqProto::OnFetchEvents);
		if (!m_bFirstBos)
			pReq->timeout = 62000;
		ExecuteRequest(pReq);

		m_bFirstBos = false;
	}

	debugLogA("Polling thread ended");
	m_hPollThread = nullptr;
}
