/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "AuthCodes.h"
#include "BattlenetBitStream.h"
#include "BattlenetSocket.h"
#include "Database/DatabaseEnv.h"
#include "HmacHash.h"
#include "Log.h"
#include "RealmList.h"
#include "SHA256.h"
#include <map>

uint32 const Battlenet::Socket::SRP6_V_Size = 128;
uint32 const Battlenet::Socket::SRP6_S_Size = 32;

std::map<Battlenet::PacketHeader, Battlenet::Socket::PacketHandler> InitHandlers()
{
    std::map<Battlenet::PacketHeader, Battlenet::Socket::PacketHandler> handlers;

    handlers[Battlenet::PacketHeader(Battlenet::CMSG_AUTH_CHALLENGE, Battlenet::AUTHENTICATION)] = &Battlenet::Socket::HandleAuthChallenge;
    handlers[Battlenet::PacketHeader(Battlenet::CMSG_AUTH_CHALLENGE_NEW, Battlenet::AUTHENTICATION)] = &Battlenet::Socket::HandleAuthChallenge;
    handlers[Battlenet::PacketHeader(Battlenet::CMSG_AUTH_PROOF_RESPONSE, Battlenet::AUTHENTICATION)] = &Battlenet::Socket::HandleAuthProofResponse;

    handlers[Battlenet::PacketHeader(Battlenet::CMSG_PING, Battlenet::CREEP)] = &Battlenet::Socket::HandlePing;
    handlers[Battlenet::PacketHeader(Battlenet::CMSG_ENABLE_ENCRYPTION, Battlenet::CREEP)] = &Battlenet::Socket::HandleEnableEncryption;

    handlers[Battlenet::PacketHeader(Battlenet::CMSG_REALM_UPDATE_SUBSCRIBE, Battlenet::WOW)] = &Battlenet::Socket::HandleRealmUpdateSubscribe;
    handlers[Battlenet::PacketHeader(Battlenet::CMSG_JOIN_REQUEST, Battlenet::WOW)] = &Battlenet::Socket::HandleRealmJoinRequest;

    return handlers;
}

std::map<Battlenet::PacketHeader, Battlenet::Socket::PacketHandler> Handlers = InitHandlers();

Battlenet::Socket::ModuleHandler const Battlenet::Socket::ModuleHandlers[MODULE_COUNT] =
{
    &Battlenet::Socket::HandlePasswordModule,
    &Battlenet::Socket::UnhandledModule,
    &Battlenet::Socket::UnhandledModule,
    &Battlenet::Socket::HandleSelectGameAccountModule,
    &Battlenet::Socket::HandleRiskFingerprintModule,
};

Battlenet::Socket::Socket(RealmSocket& socket) : _socket(socket), _accountId(0), _accountName(), _locale(),
    _os(), _build(0), _gameAccountId(0), _accountSecurityLevel(SEC_PLAYER)
{
    static uint8 const N_Bytes[] =
    {
        0xAB, 0x24, 0x43, 0x63, 0xA9, 0xC2, 0xA6, 0xC3, 0x3B, 0x37, 0xE4, 0x61, 0x84, 0x25, 0x9F, 0x8B,
        0x3F, 0xCB, 0x8A, 0x85, 0x27, 0xFC, 0x3D, 0x87, 0xBE, 0xA0, 0x54, 0xD2, 0x38, 0x5D, 0x12, 0xB7,
        0x61, 0x44, 0x2E, 0x83, 0xFA, 0xC2, 0x21, 0xD9, 0x10, 0x9F, 0xC1, 0x9F, 0xEA, 0x50, 0xE3, 0x09,
        0xA6, 0xE5, 0x5E, 0x23, 0xA7, 0x77, 0xEB, 0x00, 0xC7, 0xBA, 0xBF, 0xF8, 0x55, 0x8A, 0x0E, 0x80,
        0x2B, 0x14, 0x1A, 0xA2, 0xD4, 0x43, 0xA9, 0xD4, 0xAF, 0xAD, 0xB5, 0xE1, 0xF5, 0xAC, 0xA6, 0x13,
        0x1C, 0x69, 0x78, 0x64, 0x0B, 0x7B, 0xAF, 0x9C, 0xC5, 0x50, 0x31, 0x8A, 0x23, 0x08, 0x01, 0xA1,
        0xF5, 0xFE, 0x31, 0x32, 0x7F, 0xE2, 0x05, 0x82, 0xD6, 0x0B, 0xED, 0x4D, 0x55, 0x32, 0x41, 0x94,
        0x29, 0x6F, 0x55, 0x7D, 0xE3, 0x0F, 0x77, 0x19, 0xE5, 0x6C, 0x30, 0xEB, 0xDE, 0xF6, 0xA7, 0x86
    };

    N.SetBinary(N_Bytes, sizeof(N_Bytes));
    g.SetDword(2);

    SHA256Hash sha;
    sha.UpdateBigNumbers(&N, &g, NULL);
    sha.Finalize();
    k.SetBinary(sha.GetDigest(), sha.GetLength());
}

void Battlenet::Socket::_SetVSFields(std::string const& pstr)
{
    s.SetRand(SRP6_S_Size * 8);

    BigNumber p;
    p.SetHexStr(pstr.c_str());

    SHA256Hash sha;
    sha.UpdateBigNumbers(&s, &p, NULL);
    sha.Finalize();
    BigNumber x;
    x.SetBinary(sha.GetDigest(), sha.GetLength());
    v = g.ModExp(x, N);

    char* v_hex = v.AsHexStr();
    char* s_hex = s.AsHexStr();

    LoginDatabase.PExecute("UPDATE battlenet_accounts SET s = '%s', v = '%s' WHERE email ='%s'", s_hex, v_hex, _accountName.c_str());

    OPENSSL_free(v_hex);
    OPENSSL_free(s_hex);
}

ACE_INET_Addr const& Battlenet::Socket::GetAddressForClient(Realm const& realm, ACE_INET_Addr const& clientAddr)
{
    // Attempt to send best address for client
    if (clientAddr.is_loopback())
    {
        // Try guessing if realm is also connected locally
        if (realm.LocalAddress.is_loopback() || realm.ExternalAddress.is_loopback())
            return clientAddr;

        // Assume that user connecting from the machine that authserver is located on
        // has all realms available in his local network
        return realm.LocalAddress;
    }

    // Check if connecting client is in the same network
    if (IsIPAddrInNetwork(realm.LocalAddress, clientAddr, realm.LocalSubnetMask))
        return realm.LocalAddress;

    // Return external IP
    return realm.ExternalAddress;
}

bool Battlenet::Socket::HandleAuthChallenge(PacketHeader& header, BitStream& packet)
{

    // Verify that this IP is not in the ip_banned table
    LoginDatabase.Execute(LoginDatabase.GetPreparedStatement(LOGIN_DEL_EXPIRED_IP_BANS));

    std::string const& ip_address = _socket.getRemoteAddress();
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_IP_BANNED);
    stmt->setString(0, ip_address);
    if (PreparedQueryResult result = LoginDatabase.Query(stmt))
    {
        AuthComplete complete;
        complete.SetAuthResult(LOGIN_BANNED);
        Send(complete);
        sLog->outDebug(LOG_FILTER_AUTHSERVER, "[Battlenet::AuthChallenge] Banned ip '%s:%d' tries to login!", _socket.getRemoteAddress().c_str(), _socket.getRemotePort());
        return true;
    }

    AuthChallenge info(header, packet);
    info.Read();

    if (info.Program != "WoW")
    {
        AuthComplete complete;
        complete.SetAuthResult(AUTH_INVALID_PROGRAM);
        Send(complete);
        return true;
    }

    if (!sBattlenetMgr->HasPlatform(info.Platform))
    {
        AuthComplete complete;
        complete.SetAuthResult(AUTH_INVALID_OS);
        Send(complete);
        return true;
    }

    if (!sBattlenetMgr->HasPlatform(info.Locale))
    {
        AuthComplete complete;
        complete.SetAuthResult(AUTH_UNSUPPORTED_LANGUAGE);
        Send(complete);
        return true;
    }

    for (Component const& component : info.Components)
    {
        if (!sBattlenetMgr->HasComponent(&component))
        {
            AuthComplete complete;
            if (!sBattlenetMgr->HasProgram(component.Program))
                complete.SetAuthResult(AUTH_INVALID_PROGRAM);
            else if (!sBattlenetMgr->HasPlatform(component.Platform))
                complete.SetAuthResult(AUTH_INVALID_OS);
            else
                complete.SetAuthResult(AUTH_REGION_BAD_VERSION);

            Send(complete);
            return true;
        }

        if (component.Platform == "base")
            _build = component.Build;
    }

    _accountName = info.Login;
    _locale = info.Locale;
    _os = info.Platform;

    Utf8ToUpperOnlyLatin(_accountName);
    stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_ACCOUNT_INFO);
    stmt->setString(0, _accountName);

    PreparedQueryResult result = LoginDatabase.Query(stmt);
    if (!result)
    {
        AuthComplete complete;
        complete.SetAuthResult(AUTH_UNKNOWN_ACCOUNT);
        Send(complete);
        return true;
    }

    Field* fields = result->Fetch();

    _accountId = fields[1].GetUInt32();

    // If the IP is 'locked', check that the player comes indeed from the correct IP address
    if (fields[2].GetUInt8() == 1)                  // if ip is locked
    {
        sLog->outDebug(LOG_FILTER_AUTHSERVER,"[Battlenet::AuthChallenge] Account '%s' is locked to IP - '%s' is logging in from '%s'", _accountName.c_str(), fields[4].GetCString(), ip_address.c_str());

        if (strcmp(fields[4].GetCString(), ip_address.c_str()) != 0)
        {
            AuthComplete complete;
            complete.SetAuthResult(AUTH_ACCOUNT_LOCKED);
            Send(complete);
            return true;
        }
    }
    else
    {
        sLog->outDebug(LOG_FILTER_AUTHSERVER,"[Battlenet::AuthChallenge] Account '%s' is not locked to ip", _accountName.c_str());
        std::string accountCountry = fields[3].GetString();
        if (accountCountry.empty() || accountCountry == "00")
            sLog->outDebug(LOG_FILTER_AUTHSERVER,"[Battlenet::AuthChallenge] Account '%s' is not locked to country", _accountName.c_str());
        else if (!accountCountry.empty())
        {
            uint32 ip = inet_addr(ip_address.c_str());
            EndianConvertReverse(ip);

            stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_LOGON_COUNTRY);
            stmt->setUInt32(0, ip);
            if (PreparedQueryResult sessionCountryQuery = LoginDatabase.Query(stmt))
            {
                std::string loginCountry = (*sessionCountryQuery)[0].GetString();
                sLog->outDebug(LOG_FILTER_AUTHSERVER, "[Battlenet::AuthChallenge] Account '%s' is locked to country: '%s' Player country is '%s'", _accountName.c_str(), accountCountry.c_str(), loginCountry.c_str());
                if (loginCountry != accountCountry)
                {
                    AuthComplete complete;
                    complete.SetAuthResult(AUTH_ACCOUNT_LOCKED);
                    Send(complete);
                    return true;
                }
            }
        }
    }

    //set expired bans to inactive
    LoginDatabase.DirectExecute(LoginDatabase.GetPreparedStatement(LOGIN_DEL_BNET_EXPIRED_BANS));

    // If the account is banned, reject the logon attempt
    stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_ACTIVE_ACCOUNT_BAN);
    stmt->setUInt32(0, _accountId);
    PreparedQueryResult banresult = LoginDatabase.Query(stmt);
    if (banresult)
    {
        Field* fields = banresult->Fetch();
        if (fields[0].GetUInt32() == fields[1].GetUInt32())
        {
            AuthComplete complete;
            complete.SetAuthResult(LOGIN_BANNED);
            Send(complete);
            sLog->outDebug(LOG_FILTER_AUTHSERVER, "'%s:%d' [Battlenet::AuthChallenge] Banned account %s tried to login!", _socket.getRemoteAddress().c_str(), _socket.getRemotePort(), _accountName.c_str());
            return true;
        }
        else
        {
            AuthComplete complete;
            complete.SetAuthResult(LOGIN_SUSPENDED);
            Send(complete);
            sLog->outDebug(LOG_FILTER_AUTHSERVER, "'%s:%d' [Battlenet::AuthChallenge] Temporarily banned account %s tried to login!", _socket.getRemoteAddress().c_str(), _socket.getRemotePort(), _accountName.c_str());
            return true;
        }
    }

    SHA256Hash sha;
    sha.UpdateData(_accountName);
    sha.Finalize();

    I.SetBinary(sha.GetDigest(), sha.GetLength());

    ModuleInfo* password = new ModuleInfo(*sBattlenetMgr->GetModule({ _os, "Password" }));
    ModuleInfo* thumbprint = new ModuleInfo(*sBattlenetMgr->GetModule({ _os, "Thumbprint" }));

    std::string pStr = fields[0].GetString();

    std::string databaseV = fields[5].GetString();
    std::string databaseS = fields[6].GetString();

    if (databaseV.size() != SRP6_V_Size * 2 || databaseS.size() != SRP6_S_Size * 2)
        _SetVSFields(pStr);
    else
    {
        s.SetHexStr(databaseS.c_str());
        v.SetHexStr(databaseV.c_str());
    }

    b.SetRand(128 * 8);
    B = ((v * k) + g.ModExp(b, N)) % N;

    BitStream passwordData;
    uint8 state = 0;
    passwordData.WriteBytes(&state, 1);
    passwordData.WriteBytes(I.AsByteArray(32).get(), 32);
    passwordData.WriteBytes(s.AsByteArray(32).get(), 32);
    passwordData.WriteBytes(B.AsByteArray(128).get(), 128);
    passwordData.WriteBytes(b.AsByteArray(128).get(), 128);

    password->DataSize = passwordData.GetSize();
    password->Data = new uint8[password->DataSize];
    memcpy(password->Data, passwordData.GetBuffer(), password->DataSize);

    _modulesWaitingForData.push(MODULE_PASSWORD);

    ProofRequest request;
    request.Modules.push_back(password);
    // if has authenticator, send Token module
    request.Modules.push_back(thumbprint);
    Send(request);
    return true;
}

bool Battlenet::Socket::HandleAuthProofResponse(PacketHeader& header, BitStream& packet)
{
    ProofResponse proof(header, packet);
    proof.Read();

    if (_modulesWaitingForData.size() < proof.Modules.size())
    {
        AuthComplete complete;
        complete.SetAuthResult(AUTH_CORRUPTED_MODULE);
        Send(complete);
        return true;
    }

    ServerPacket* response = nullptr;
    for (size_t i = 0; i < proof.Modules.size(); ++i)
    {
        if (!(this->*(ModuleHandlers[_modulesWaitingForData.front()]))(proof.Modules[i], &response))
            break;

        _modulesWaitingForData.pop();
    }

    if (!response)
    {
        response = new AuthComplete();
        static_cast<AuthComplete*>(response)->SetAuthResult(AUTH_INTERNAL_ERROR);
    }

    Send(*response);
    delete response;
    return true;
}

bool Battlenet::Socket::HandlePing(PacketHeader& /*header*/, BitStream& /*packet*/)
{
    Pong pong;
    Send(pong);
    return true;
}

bool Battlenet::Socket::HandleEnableEncryption(PacketHeader& /*header*/, BitStream& packet)
{
    _crypt.Init(&K);
    _crypt.DecryptRecv(packet.GetBuffer() + 2, packet.GetSize() - 2);
    return false;
}

bool Battlenet::Socket::HandleRealmUpdateSubscribe(PacketHeader& /*header*/, BitStream& /*packet*/)
{
    sRealmList->UpdateIfNeed();

    RealmCharacterCounts counts;

    ACE_INET_Addr clientAddr;
    _socket.peer().get_remote_addr(clientAddr);

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_CHARACTER_COUNTS);
    stmt->setUInt32(0, _gameAccountId);

    if (PreparedQueryResult countResult = LoginDatabase.Query(stmt))
    {
        do
        {
            Field* fields = countResult->Fetch();
            uint32 build = fields[4].GetUInt32();
            counts.CharacterCounts.push_back({ { fields[2].GetUInt8(), fields[3].GetUInt8(), fields[1].GetUInt32(), (_build != build ? build : 0) }, fields[0].GetUInt8() });
        } while (countResult->NextRow());
    }

    for (RealmList::RealmMap::const_iterator i = sRealmList->begin(); i != sRealmList->end(); ++i)
    {
        Realm const& realm = i->second;

        uint32 flag = realm.flag;
        RealmBuildInfo const* buildInfo = AuthHelper::GetBuildInfo(realm.gamebuild);
        if (realm.gamebuild != _build)
        {
            if (!buildInfo)
                continue;

            flag |= REALM_FLAG_OFFLINE | REALM_FLAG_SPECIFYBUILD;   // tell the client what build the realm is for
        }

        if (!buildInfo)
            flag &= ~REALM_FLAG_SPECIFYBUILD;

        RealmUpdate* update = new RealmUpdate();
        update->Timezone = realm.timezone;
        update->Population = realm.populationLevel;
        update->Lock = (realm.allowedSecurityLevel > _accountSecurityLevel) ? 1 : 0;
        update->Type = realm.icon;
        update->Name = realm.name;

        if (flag & REALM_FLAG_SPECIFYBUILD)
        {
            std::ostringstream version;
            version << buildInfo->MajorVersion << '.' << buildInfo->MinorVersion << '.' << buildInfo->BugfixVersion << '.' << buildInfo->HotfixVersion;

            update->Version = version.str();
            update->Address = GetAddressForClient(realm, clientAddr);
            update->Build = realm.gamebuild;
        }

        update->Flags = flag;
        update->Region = realm.Region;
        update->Battlegroup = realm.Battlegroup;
        update->Index = realm.m_ID;

        counts.RealmData.push_back(update);
    }

    counts.RealmData.push_back(new RealmUpdateComplete());

    Send(counts);
    return true;
}

bool Battlenet::Socket::HandleRealmJoinRequest(PacketHeader& header, BitStream& packet)
{
    RealmJoinRequest join(header, packet);
    join.Read();

    RealmJoinResult result;
    Realm const* realm = sRealmList->GetRealm(join.Realm);
    if (!realm)
    {
        Send(result);
        return true;
    }

    result.ServerSeed = uint32(rand32());

    uint8 sessionKey[40];
    HmacHash hmac(K.GetNumBytes(), K.AsByteArray().get(), EVP_sha1(), SHA_DIGEST_LENGTH);
    hmac.UpdateData((uint8*)"WoW\0", 4);
    hmac.UpdateData((uint8*)&join.ClientSeed, 4);
    hmac.UpdateData((uint8*)&result.ServerSeed, 4);
    hmac.Finalize();

    memcpy(sessionKey, hmac.GetDigest(), hmac.GetLength());

    HmacHash hmac2(K.GetNumBytes(), K.AsByteArray().get(), EVP_sha1(), SHA_DIGEST_LENGTH);
    hmac2.UpdateData((uint8*)"WoW\0", 4);
    hmac2.UpdateData((uint8*)&result.ServerSeed, 4);
    hmac2.UpdateData((uint8*)&join.ClientSeed, 4);
    hmac2.Finalize();

    memcpy(sessionKey + hmac.GetLength(), hmac2.GetDigest(), hmac2.GetLength());

    LoginDatabase.DirectPExecute("UPDATE account SET sessionkey = '%s', last_ip = '%s', last_login = NOW(), locale = %u, failed_logins = 0, os = '%s' WHERE id = %u",
        ByteArrayToHexStr(sessionKey, 40, true).c_str(), _socket.getRemoteAddress().c_str(), GetLocaleByName(_locale), _os.c_str(), _gameAccountId);

    result.IPv4.push_back(realm->ExternalAddress);
    if (realm->ExternalAddress != realm->LocalAddress)
        result.IPv4.push_back(realm->LocalAddress);

    Send(result);
    return true;
}

void Battlenet::Socket::OnRead()
{
    size_t length = _socket.recv_len();
    if (!length)
        return;

    BitStream packet(length);
    if (!_socket.recv((char*)packet.GetBuffer(), length))
        return;

    _crypt.DecryptRecv(packet.GetBuffer(), length);

    while (!packet.IsRead())
    {
        try
        {
            PacketHeader header;
            header.Opcode = packet.Read<uint32>(6);
            if (packet.Read<bool>(1))
                header.Channel = packet.Read<int32>(4);

            sLog->outTrace(LOG_FILTER_AUTHSERVER, "Battlenet::Socket::OnRead %s", header.ToString().c_str());
            std::map<PacketHeader, PacketHandler>::const_iterator itr = Handlers.find(header);
            if (itr != Handlers.end())
            {
                if ((this->*(itr->second))(header, packet))
                    break;
            }
            else 
            {
                sLog->outDebug(LOG_FILTER_AUTHSERVER, "Battlenet::Socket::OnRead Unhandled opcode %s", header.ToString().c_str());
                return;
            }

            packet.AlignToNextByte();
        }
        catch (BitStreamPositionException const& e)
        {
            sLog->outError(LOG_FILTER_AUTHSERVER, "Battlenet::Socket::OnRead Exception: %s", e.what());
            _socket.shutdown();
            return;
        }
    }
}

void Battlenet::Socket::OnAccept()
{
    sLog->outTrace(LOG_FILTER_AUTHSERVER, "Battlenet::Socket::OnAccept");
}

void Battlenet::Socket::OnClose()
{
    sLog->outTrace(LOG_FILTER_AUTHSERVER, "Battlenet::Socket::OnClose");
}

void Battlenet::Socket::Send(ServerPacket& packet)
{
    sLog->outTrace(LOG_FILTER_AUTHSERVER, "Battlenet::Socket::Send %s", packet.ToString().c_str());

    packet.Write();

    _crypt.EncryptSend(const_cast<uint8*>(packet.GetData()), packet.GetSize());
    _socket.send(reinterpret_cast<char const*>(packet.GetData()), packet.GetSize());
}

inline void ReplaceResponse(Battlenet::ServerPacket** oldResponse, Battlenet::ServerPacket* newResponse)
{
    if (*oldResponse)
        delete *oldResponse;

    *oldResponse = newResponse;
}

bool Battlenet::Socket::HandlePasswordModule(BitStream* dataStream, ServerPacket** response)
{
    if (dataStream->GetSize() != 1 + 128 + 32 + 128)
    {
        AuthComplete* complete = new AuthComplete();
        complete->SetAuthResult(AUTH_CORRUPTED_MODULE);
        ReplaceResponse(response, complete);
        return false;
    }

    if (dataStream->Read<uint8>(8) != 2)                // State
    {
        AuthComplete* complete = new AuthComplete();
        complete->SetAuthResult(AUTH_CORRUPTED_MODULE);
        ReplaceResponse(response, complete);
        return false;
    }


    BigNumber A, clientM1, clientChallenge;
    A.SetBinary(dataStream->ReadBytes(128).get(), 128);
    clientM1.SetBinary(dataStream->ReadBytes(32).get(), 32);
    clientChallenge.SetBinary(dataStream->ReadBytes(128).get(), 128);

    if (A.isZero())
    {
        AuthComplete* complete = new AuthComplete();
        complete->SetAuthResult(AUTH_CORRUPTED_MODULE);
        ReplaceResponse(response, complete);
        return false;
    }

    SHA256Hash sha;
    sha.UpdateBigNumbers(&A, &B, NULL);
    sha.Finalize();

    BigNumber u;
    u.SetBinary(sha.GetDigest(), sha.GetLength());

    BigNumber S = ((A * v.ModExp(u, N)) % N).ModExp(b, N);

    uint8 S_bytes[128];
    memcpy(S_bytes, S.AsByteArray(128).get(), 128);

    uint8 part1[64];
    uint8 part2[64];

    for (int i = 0; i < 64; ++i)
    {
        part1[i] = S_bytes[i * 2];
        part2[i] = S_bytes[i * 2 + 1];
    }

    SHA256Hash part1sha, part2sha;
    part1sha.UpdateData(part1, 64);
    part1sha.Finalize();
    part2sha.UpdateData(part2, 64);
    part2sha.Finalize();

    uint8 sessionKey[SHA256_DIGEST_LENGTH * 2];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        sessionKey[i * 2] = part1sha.GetDigest()[i];
        sessionKey[i * 2 + 1] = part2sha.GetDigest()[i];
    }

    K.SetBinary(sessionKey, SHA256_DIGEST_LENGTH * 2);

    BigNumber M1;

    uint8 hash[SHA256_DIGEST_LENGTH];
    sha.Initialize();
    sha.UpdateBigNumbers(&N, NULL);
    sha.Finalize();
    memcpy(hash, sha.GetDigest(), sha.GetLength());

    sha.Initialize();
    sha.UpdateBigNumbers(&g, NULL);
    sha.Finalize();

    for (int i = 0; i < sha.GetLength(); ++i)
        hash[i] ^= sha.GetDigest()[i];

    SHA256Hash shaI;
    shaI.UpdateData(ByteArrayToHexStr(I.AsByteArray().get(), 32));
    shaI.Finalize();

    // Concat all variables for M1 hash
    sha.Initialize();
    sha.UpdateData(hash, SHA256_DIGEST_LENGTH);
    sha.UpdateData(shaI.GetDigest(), shaI.GetLength());
    sha.UpdateBigNumbers(&s, &A, &B, &K, NULL);
    sha.Finalize();

    M1.SetBinary(sha.GetDigest(), sha.GetLength());

    if (memcmp(M1.AsByteArray().get(), clientM1.AsByteArray().get(), 32))
    {
        AuthComplete* complete = new AuthComplete();
        complete->SetAuthResult(AUTH_UNKNOWN_ACCOUNT);
        ReplaceResponse(response, complete);
        return false;
    }

    uint64 numAccounts = 0;
    QueryResult result = LoginDatabase.PQuery("SELECT a.username, a.id, ab.bandate, ab.unbandate, ab.active FROM account a LEFT JOIN account_banned ab ON a.id = ab.id WHERE battlenet_account = '%u'", _accountId);
    //PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_GAME_ACCOUNTS);
    //stmt->setUInt32(0, _accountId);
    //PreparedQueryResult result = LoginDatabase.Query(stmt);
    if (result)
        numAccounts = result->GetRowCount();

    if (!numAccounts)
    {
        AuthComplete* noAccounts = new AuthComplete();
        noAccounts->SetAuthResult(LOGIN_NO_GAME_ACCOUNT);
        ReplaceResponse(response, noAccounts);
        return false;
    }

    Field* fields = result->Fetch();

    //set expired game account bans to inactive
    LoginDatabase.DirectExecute(LoginDatabase.GetPreparedStatement(LOGIN_UPD_EXPIRED_ACCOUNT_BANS));

    BigNumber M;
    sha.Initialize();
    sha.UpdateBigNumbers(&A, &M1, &K, NULL);
    sha.Finalize();
    M.SetBinary(sha.GetDigest(), sha.GetLength());

    BitStream stream;
    ModuleInfo* password = new ModuleInfo(*sBattlenetMgr->GetModule({ _os, "Password" }));
    uint8 state = 3;

    stream.WriteBytes(&state, 1);
    stream.WriteBytes(M.AsByteArray(32).get(), 32);
    stream.WriteBytes(b.AsByteArray(128).get(), 128);

    password->DataSize = stream.GetSize();
    password->Data = new uint8[password->DataSize];
    memcpy(password->Data, stream.GetBuffer(), password->DataSize);

    ProofRequest* request = new ProofRequest();
    request->Modules.push_back(password);
    if (numAccounts > 1)
    {
        BitStream accounts;
        state = 0;
        accounts.WriteBytes(&state, 1);
        accounts.Write(numAccounts, 8);
        do
        {
            fields = result->Fetch();
            accounts.Write(2, 8);
            accounts.WriteString(fields[0].GetString(), 8);
        } while (result->NextRow());

        ModuleInfo* selectGameAccount = new ModuleInfo(*sBattlenetMgr->GetModule({ _os, "SelectGameAccount" }));
        selectGameAccount->DataSize = accounts.GetSize();
        selectGameAccount->Data = new uint8[selectGameAccount->DataSize];
        memcpy(selectGameAccount->Data, accounts.GetBuffer(), selectGameAccount->DataSize);
        request->Modules.push_back(selectGameAccount);
        _modulesWaitingForData.push(MODULE_SELECT_GAME_ACCOUNT);
    }
    else
    {
        if (fields[4].GetBool())
        {
            delete request;

            AuthComplete* complete = new AuthComplete();
            if (fields[2].GetUInt32() == fields[3].GetUInt32())
            {
                complete->SetAuthResult(LOGIN_BANNED);
                sLog->outDebug(LOG_FILTER_AUTHSERVER, "'%s:%d' [Battlenet::AuthChallenge] Banned account %s tried to login!", _socket.getRemoteAddress().c_str(), _socket.getRemotePort(), _accountName.c_str());
            }
            else
            {
                complete->SetAuthResult(LOGIN_SUSPENDED);
                sLog->outDebug(LOG_FILTER_AUTHSERVER, "'%s:%d' [Battlenet::AuthChallenge] Temporarily banned account %s tried to login!", _socket.getRemoteAddress().c_str(), _socket.getRemotePort(), _accountName.c_str());
            }

            ReplaceResponse(response, complete);
            return true;
        }

        _gameAccountId = (*result)[1].GetUInt32();

        request->Modules.push_back(new ModuleInfo(*sBattlenetMgr->GetModule({ _os, "RiskFingerprint" })));
        _modulesWaitingForData.push(MODULE_RISK_FINGERPRINT);
    }

    ReplaceResponse(response, request);

    return true;
}

bool Battlenet::Socket::HandleSelectGameAccountModule(BitStream* dataStream, ServerPacket** response)
{
    if (dataStream->Read<uint8>(8) != 1)
    {
        AuthComplete* complete = new AuthComplete();
        complete->SetAuthResult(AUTH_CORRUPTED_MODULE);
        ReplaceResponse(response, complete);
        return false;
    }

    dataStream->Read<uint8>(8);
    std::string account = dataStream->ReadString(8);

    
    LoginDatabase.EscapeString(account);
    QueryResult result = LoginDatabase.PQuery("SELECT a.id, ab.bandate, ab.unbandate, ab.active FROM account a LEFT JOIN account_banned ab ON a.id = ab.id WHERE username = '%s' AND battlenet_account = '%u'", account.c_str(), _accountId);   
    //PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_GAME_ACCOUNT);
    //stmt->setString(0, account);
    //stmt->setUInt32(1, _accountId);
    //PreparedQueryResult result = LoginDatabase.Query(stmt);
    if (!result)
    {
        AuthComplete* complete = new AuthComplete();
        complete->SetAuthResult(LOGIN_NO_GAME_ACCOUNT);
        ReplaceResponse(response, complete);
        return false;
    }

    Field* fields = result->Fetch();
    if (fields[3].GetBool())
    {
        AuthComplete* complete = new AuthComplete();
        if (fields[1].GetUInt32() == fields[2].GetUInt32())
        {
            complete->SetAuthResult(LOGIN_BANNED);
            sLog->outDebug(LOG_FILTER_AUTHSERVER, "'%s:%d' [Battlenet::AuthChallenge] Banned account %s tried to login!", _socket.getRemoteAddress().c_str(), _socket.getRemotePort(), _accountName.c_str());
        }
        else
        {
            complete->SetAuthResult(LOGIN_SUSPENDED);
            sLog->outDebug(LOG_FILTER_AUTHSERVER, "'%s:%d' [Battlenet::AuthChallenge] Temporarily banned account %s tried to login!", _socket.getRemoteAddress().c_str(), _socket.getRemotePort(), _accountName.c_str());
        }

        ReplaceResponse(response, complete);
        return true;
    }

    _gameAccountId = fields[0].GetUInt32();

    ProofRequest* request = new ProofRequest();
    request->Modules.push_back(new ModuleInfo(*sBattlenetMgr->GetModule({ _os, "RiskFingerprint" })));
    ReplaceResponse(response, request);

    _modulesWaitingForData.push(MODULE_RISK_FINGERPRINT);
    return true;
}

bool Battlenet::Socket::HandleRiskFingerprintModule(BitStream* dataStream, ServerPacket** response)
{
    AuthComplete* complete = new AuthComplete();
    if (dataStream->Read<uint8>(8) == 1)
    {
        std::ostringstream str;
        str << _gameAccountId << "#1";

        complete->GameAccountId = _gameAccountId;
        complete->GameAccountName = str.str();
        complete->AccountFlags = 0x800000;      // 0x1 IsGMAccount, 0x8 IsTrialAccount, 0x800000 IsProPassAccount

        PreparedStatement *stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_BNET_LAST_LOGIN_INFO);
        stmt->setString(0, _socket.getRemoteAddress());
        stmt->setUInt8(1, GetLocaleByName(_locale));
        stmt->setString(2, _os);
        stmt->setUInt32(3, _accountId);
        LoginDatabase.Execute(stmt);
    }
    else
        complete->SetAuthResult(AUTH_BAD_VERSION_HASH);

    ReplaceResponse(response, complete);
    return false;
}

bool Battlenet::Socket::UnhandledModule(BitStream* /*dataStream*/, ServerPacket** response)
{
    AuthComplete* complete = new AuthComplete();
    complete->SetAuthResult(AUTH_CORRUPTED_MODULE);
    ReplaceResponse(response, complete);
    return false;
}