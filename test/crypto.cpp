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
/** @file crypto.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * Crypto test functions.
 */

#include <random>
#include <secp256k1/secp256k1.h>
#include <libdevcore/Common.h>
#include <libdevcore/RLP.h>
#include <libdevcore/Log.h>
#include <libethereum/Transaction.h>
#include <boost/test/unit_test.hpp>
#include "TestHelperCrypto.h"

using namespace std;
using namespace dev;

namespace dev
{
namespace crypto
{

inline CryptoPP::AutoSeededRandomPool& PRNG() {
	static CryptoPP::AutoSeededRandomPool prng;
	return prng;
}
	
}
}

using namespace CryptoPP;

BOOST_AUTO_TEST_SUITE(crypto)

BOOST_AUTO_TEST_CASE(cryptopp_ecies_message)
{
	cnote << "Testing cryptopp_ecies_message...";

	string const message("Now is the time for all good men to come to the aide of humanity.");

	AutoSeededRandomPool prng;

	ECIES<ECP>::Decryptor localDecryptor(prng, ASN1::secp256r1());
	SavePrivateKey(localDecryptor.GetPrivateKey());
	
	ECIES<ECP>::Encryptor localEncryptor(localDecryptor);
	SavePublicKey(localEncryptor.GetPublicKey());

	ECIES<ECP>::Decryptor futureDecryptor;
	LoadPrivateKey(futureDecryptor.AccessPrivateKey());
	futureDecryptor.GetPrivateKey().ThrowIfInvalid(prng, 3);
	
	ECIES<ECP>::Encryptor futureEncryptor;
	LoadPublicKey(futureEncryptor.AccessPublicKey());
	futureEncryptor.GetPublicKey().ThrowIfInvalid(prng, 3);

	// encrypt/decrypt with local
	string cipherLocal;
	StringSource ss1 (message, true, new PK_EncryptorFilter(prng, localEncryptor, new StringSink(cipherLocal) ) );
	string plainLocal;
	StringSource ss2 (cipherLocal, true, new PK_DecryptorFilter(prng, localDecryptor, new StringSink(plainLocal) ) );

	// encrypt/decrypt with future
	string cipherFuture;
	StringSource ss3 (message, true, new PK_EncryptorFilter(prng, futureEncryptor, new StringSink(cipherFuture) ) );
	string plainFuture;
	StringSource ss4 (cipherFuture, true, new PK_DecryptorFilter(prng, futureDecryptor, new StringSink(plainFuture) ) );
	
	// decrypt local w/future
	string plainFutureFromLocal;
	StringSource ss5 (cipherLocal, true, new PK_DecryptorFilter(prng, futureDecryptor, new StringSink(plainFutureFromLocal) ) );
	
	// decrypt future w/local
	string plainLocalFromFuture;
	StringSource ss6 (cipherFuture, true, new PK_DecryptorFilter(prng, localDecryptor, new StringSink(plainLocalFromFuture) ) );
	
	
	assert(plainLocal == message);
	assert(plainFuture == plainLocal);
	assert(plainFutureFromLocal == plainLocal);
	assert(plainLocalFromFuture == plainLocal);
}

BOOST_AUTO_TEST_CASE(cryptopp_ecdh_prime)
{
	cnote << "Testing cryptopp_ecdh_prime...";
	
	using namespace CryptoPP;
	OID curve = ASN1::secp256r1();

	ECDH<ECP>::Domain dhLocal(curve);
	SecByteBlock privLocal(dhLocal.PrivateKeyLength());
	SecByteBlock pubLocal(dhLocal.PublicKeyLength());
	dhLocal.GenerateKeyPair(dev::crypto::PRNG(), privLocal, pubLocal);
	
	ECDH<ECP>::Domain dhRemote(curve);
	SecByteBlock privRemote(dhRemote.PrivateKeyLength());
	SecByteBlock pubRemote(dhRemote.PublicKeyLength());
	dhRemote.GenerateKeyPair(dev::crypto::PRNG(), privRemote, pubRemote);
	
	assert(dhLocal.AgreedValueLength() == dhRemote.AgreedValueLength());
	
	// local: send public to remote; remote: send public to local
	
	// Local
	SecByteBlock sharedLocal(dhLocal.AgreedValueLength());
	assert(dhLocal.Agree(sharedLocal, privLocal, pubRemote));
	
	// Remote
	SecByteBlock sharedRemote(dhRemote.AgreedValueLength());
	assert(dhRemote.Agree(sharedRemote, privRemote, pubLocal));
	
	// Test
	Integer ssLocal, ssRemote;
	ssLocal.Decode(sharedLocal.BytePtr(), sharedLocal.SizeInBytes());
	ssRemote.Decode(sharedRemote.BytePtr(), sharedRemote.SizeInBytes());
	
	assert(ssLocal != 0);
	assert(ssLocal == ssRemote);
}

BOOST_AUTO_TEST_CASE(cryptopp_ecdh_aes128_cbc_noauth)
{
	// ECDH gives 256-bit shared while aes uses 128-bits
	// Use first 128-bits of shared secret as symmetric key
	// IV is 0
	// New connections require new ECDH keypairs
	
	
}
	
BOOST_AUTO_TEST_CASE(cryptopp_eth_fbba)
{
	// Initial Authentication:
	//
	// New/Known Peer:
	// pubkeyL = knownR? ? myKnown : myECDH
	// pubkeyR = knownR? ? theirKnown : theirECDH
	//
	// Initial message = hmac(k=sha3(shared-secret[128..255]), address(pubkeyL)) || ECIES encrypt(pubkeyR, pubkeyL)
	//
	// Key Exchange (this could occur after handshake messages):
	// If peers do not know each other they will need to exchange public keys.
	//
	// Drop ECDH (this could occur after handshake messages):
	// After authentication and/or key exchange, both sides generate shared key
	// from their 'known' keys and use this to encrypt all future messages.
	//
	// v2: If one side doesn't trust the other then a single-use key maybe sent.
	// This will need to be tracked for future connections; when non-trusting peer
	// wants to trust the other, it can request that it's old, 'new', public key be
	// accepted. And, if the peer *really* doesn't trust the other side, it can request
	// that a new, 'new', public key be accepted.
	//
	// Handshake (all or nothing, padded):
	// All Peers (except blacklisted):
	//
	//
	// New Peer:
	//
	//
	// Known Untrusted Peer:
	//
	//
	// Known Trusted Peer:
	//
	//
	// Blacklisted Peeer:
	// Already dropped by now.
	//
	//
	// MAC:
	// ...
}
	
BOOST_AUTO_TEST_CASE(eth_keypairs)
{
	cnote << "Testing Crypto...";
	secp256k1_start();

	KeyPair p(Secret(fromHex("3ecb44df2159c26e0f995712d4f39b6f6e499b40749b1cf1246c37f9516cb6a4")));
	BOOST_REQUIRE(p.pub() == Public(fromHex("97466f2b32bc3bb76d4741ae51cd1d8578b48d3f1e68da206d47321aec267ce78549b514e4453d74ef11b0cd5e4e4c364effddac8b51bcfc8de80682f952896f")));
	BOOST_REQUIRE(p.address() == Address(fromHex("8a40bfaa73256b60764c1bf40675a99083efb075")));
	{
		eth::Transaction t;
		t.nonce = 0;
		t.receiveAddress = h160(fromHex("944400f4b88ac9589a0f17ed4671da26bddb668b"));
		t.value = 1000;
		auto rlp = t.rlp(false);
		cnote << RLP(rlp);
		cnote << toHex(rlp);
		cnote << t.sha3(false);
		t.sign(p.secret());
		rlp = t.rlp(true);
		cnote << RLP(rlp);
		cnote << toHex(rlp);
		cnote << t.sha3(true);
		BOOST_REQUIRE(t.sender() == p.address());
	}

} 
 

int cryptoTest()
{
	cnote << "Testing Crypto...";
	secp256k1_start();

	KeyPair p(Secret(fromHex("3ecb44df2159c26e0f995712d4f39b6f6e499b40749b1cf1246c37f9516cb6a4")));
	assert(p.pub() == Public(fromHex("97466f2b32bc3bb76d4741ae51cd1d8578b48d3f1e68da206d47321aec267ce78549b514e4453d74ef11b0cd5e4e4c364effddac8b51bcfc8de80682f952896f")));
	assert(p.address() == Address(fromHex("8a40bfaa73256b60764c1bf40675a99083efb075")));
	{
		eth::Transaction t;
		t.nonce = 0;
		t.receiveAddress = h160(fromHex("944400f4b88ac9589a0f17ed4671da26bddb668b"));
		t.value = 1000;
		auto rlp = t.rlp(false);
		cnote << RLP(rlp);
		cnote << toHex(rlp);
		cnote << t.sha3(false);
		t.sign(p.secret());
		rlp = t.rlp(true);
		cnote << RLP(rlp);
		cnote << toHex(rlp);
		cnote << t.sha3(true);
		assert(t.sender() == p.address());
	}


#if 0
	// Test transaction.
	bytes tx = fromHex("88005401010101010101010101010101010101010101011f0de0b6b3a76400001ce8d4a5100080181c373130a009ba1f10285d4e659568bfcfec85067855c5a3c150100815dad4ef98fd37cf0593828c89db94bd6c64e210a32ef8956eaa81ea9307194996a3b879441f5d");
	cout << "TX: " << RLP(tx) << endl;

	Transaction t2(tx);
	cout << "SENDER: " << hex << t2.sender() << dec << endl;

	secp256k1_start();

	Transaction t;
	t.nonce = 0;
	t.value = 1;			// 1 wei.
	t.receiveAddress = toAddress(sha3("123"));

	bytes sig64 = toBigEndian(t.vrs.r) + toBigEndian(t.vrs.s);
	cout << "SIG: " << sig64.size() << " " << toHex(sig64) << " " << t.vrs.v << endl;

	auto msg = t.rlp(false);
	cout << "TX w/o SIG: " << RLP(msg) << endl;
	cout << "RLP(TX w/o SIG): " << toHex(t.rlpString(false)) << endl;
	std::string hmsg = sha3(t.rlpString(false), false);
	cout << "SHA256(RLP(TX w/o SIG)): 0x" << toHex(hmsg) << endl;

	bytes privkey = sha3Bytes("123");

	{
		bytes pubkey(65);
		int pubkeylen = 65;

		int ret = secp256k1_ecdsa_seckey_verify(privkey.data());
		cout << "SEC: " << dec << ret << " " << toHex(privkey) << endl;

		ret = secp256k1_ecdsa_pubkey_create(pubkey.data(), &pubkeylen, privkey.data(), 1);
		pubkey.resize(pubkeylen);
		int good = secp256k1_ecdsa_pubkey_verify(pubkey.data(), (int)pubkey.size());
		cout << "PUB: " << dec << ret << " " << pubkeylen << " " << toHex(pubkey) << (good ? " GOOD" : " BAD") << endl;
	}

	// Test roundtrip...
	{
		bytes sig(64);
		u256 nonce = 0;
		int v = 0;
		cout << toHex(hmsg) << endl;
		cout << toHex(privkey) << endl;
		cout << hex << nonce << dec << endl;
		int ret = secp256k1_ecdsa_sign_compact((byte const*)hmsg.data(), (int)hmsg.size(), sig.data(), privkey.data(), (byte const*)&nonce, &v);
		cout << "MYSIG: " << dec << ret << " " << sig.size() << " " << toHex(sig) << " " << v << endl;

		bytes pubkey(65);
		int pubkeylen = 65;
		ret = secp256k1_ecdsa_recover_compact((byte const*)hmsg.data(), (int)hmsg.size(), (byte const*)sig.data(), pubkey.data(), &pubkeylen, 0, v);
		pubkey.resize(pubkeylen);
		cout << "MYREC: " << dec << ret << " " << pubkeylen << " " << toHex(pubkey) << endl;
	}

	{
		bytes pubkey(65);
		int pubkeylen = 65;
		int ret = secp256k1_ecdsa_recover_compact((byte const*)hmsg.data(), (int)hmsg.size(), (byte const*)sig64.data(), pubkey.data(), &pubkeylen, 0, (int)t.vrs.v - 27);
		pubkey.resize(pubkeylen);
		cout << "RECPUB: " << dec << ret << " " << pubkeylen << " " << toHex(pubkey) << endl;
		cout << "SENDER: " << hex << toAddress(dev::sha3(bytesConstRef(&pubkey).cropped(1))) << dec << endl;
	}
#endif
	return 0;
}

BOOST_AUTO_TEST_SUITE_END()
