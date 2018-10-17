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
/** @file main.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * Main test functions.
 */

#include <fstream>
#include "../json_spirit/json_spirit_reader_template.h"
#include "../json_spirit/json_spirit_writer_template.h"
#include "TrieCommon.h"
#include "Log.h"
using namespace std;
using namespace eth;
namespace js = json_spirit;

namespace eth
{

template <> class UnitTest<3>
{
public:
	int operator()()
	{
		js::mValue v;
		string s = asString(contents("../../tests/hexencodetest.json"));
		js::read_string(s, v);
		bool passed = true;
		for (auto& i: v.get_obj())
		{
			js::mObject& o = i.second.get_obj();
			cnote << i.first;
			bytes v;
			for (auto& i: o["seq"].get_array())
				v.push_back((byte)i.get_int());
			auto e = hexPrefixEncode(v, o["term"].get_bool());
			if (!o["out"].is_null() && o["out"].get_str() != toHex(e))
			{
				cwarn << "Test failed.";
				cwarn << "Test says:" << o["out"].get_str();
				cwarn << "Impl says:" << toHex(e);
				passed = false;
			}
		}
		return passed ? 0 : 1;
	}

};

}

int hexPrefixTest()
{
	cnote << "Testing Hex-Prefix-Encode...";
	return UnitTest<3>()();
}
