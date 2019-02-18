/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
 */
/** @file FixedWebThreeStubServer.h
 * @author Marek Kotewicz <marek@ethdev.com>
 * @date 2015
 */

#pragma once

#include <libdevcore/Exceptions.h>
#include <libweb3jsonrpc/WebThreeStubServerBase.h>
#include <libweb3jsonrpc/AccountHolder.h>

/**
 * @brief dummy JSON-RPC api implementation
 * Should be used for test purposes only
 * Supports eth && db interfaces
 * Doesn't support shh && net interfaces
 */
class FixedWebThreeServer: public dev::WebThreeStubServerBase
{
public:
	FixedWebThreeServer(std::vector<dev::KeyPair> const& _allAccounts, dev::eth::Interface* _client):
		WebThreeStubServerBase(std::make_shared<dev::eth::FixedAccountHolder>([=](){return _client;}, _allAccounts), _allAccounts),
		m_client(_client)
	{}

private:
	dev::eth::Interface* client() override { return m_client; }
	std::shared_ptr<dev::shh::Interface> face() override {	BOOST_THROW_EXCEPTION(dev::InterfaceNotSupported("dev::shh::Interface")); }
	dev::bzz::Interface* bzz() override { BOOST_THROW_EXCEPTION(dev::InterfaceNotSupported("dev::bzz::Interface")); }
	dev::WebThreeNetworkFace* network() override { BOOST_THROW_EXCEPTION(dev::InterfaceNotSupported("dev::WebThreeNetworkFace")); }

private:
	dev::eth::Interface* m_client;
	std::map<std::string, std::string> m_db;
};