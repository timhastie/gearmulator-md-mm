#include "mmpatterndump.h"

#include <algorithm>
#include <cstring>

namespace md::mmPatternDump
{
	namespace
	{
		// Decoded image offsets (see MCL MNMPattern::fromSysex).
		constexpr size_t g_ampTrigs = 0, g_filterTrigs = 48, g_lfoTrigs = 96, g_offTrigs = 144;
		constexpr size_t g_triglessTrigs = 288, g_chordTrigs = 336;
		constexpr size_t g_lockPatterns = 628;
		constexpr size_t g_notes = 676;
		constexpr size_t g_patternLength = 1060;
		constexpr size_t g_locksUsed = 1366;
		constexpr size_t g_locks = 1367;

		std::vector<uint8_t> unpack7(const uint8_t* _begin, const uint8_t* _end)
		{
			std::vector<uint8_t> out;
			for(const uint8_t* p = _begin; p < _end;)
			{
				const auto msb = *p++;
				for(uint8_t i = 0; i < 7 && p < _end; ++i, ++p)
					out.push_back(static_cast<uint8_t>(*p | ((msb >> (6 - i)) & 1u) << 7));
			}
			return out;
		}

		void pack7(std::vector<uint8_t>& _out, const std::vector<uint8_t>& _in)
		{
			for(size_t i = 0; i < _in.size();)
			{
				const auto n = std::min<size_t>(7, _in.size() - i);
				uint8_t msb = 0;
				for(size_t k = 0; k < n; ++k)
					msb |= static_cast<uint8_t>((_in[i + k] >> 7) << (6 - k));
				_out.push_back(msb);
				for(size_t k = 0; k < n; ++k)
					_out.push_back(_in[i + k] & 0x7f);
				i += n;
			}
		}

		std::optional<std::vector<uint8_t>> rleExpand(const std::vector<uint8_t>& _in)
		{
			std::vector<uint8_t> out;
			for(size_t i = 0; i < _in.size(); ++i)
			{
				if(_in[i] < 0x80) { out.push_back(_in[i]); continue; }
				const size_t count = _in[i] & 0x7f;
				if(count == 0 || ++i >= _in.size() || out.size() + count > 1u << 20)
					return std::nullopt;
				out.insert(out.end(), count, _in[i]);
			}
			return out;
		}

		std::vector<uint8_t> rleCompress(const std::vector<uint8_t>& _in)
		{
			std::vector<uint8_t> out;
			for(size_t i = 0; i < _in.size();)
			{
				size_t run = 1;
				while(i + run < _in.size() && _in[i + run] == _in[i] && run < 0x7f) ++run;
				if(run == 1 && _in[i] < 0x80) out.push_back(_in[i]);
				else { out.push_back(static_cast<uint8_t>(0x80 | run)); out.push_back(_in[i]); }
				i += run;
			}
			return out;
		}

		uint64_t getU64(const std::vector<uint8_t>& _d, const size_t _off)
		{
			uint64_t v = 0;
			for(int i = 0; i < 8; ++i) v = v << 8 | _d[_off + i];
			return v;
		}
		void setU64(std::vector<uint8_t>& _d, const size_t _off, const uint64_t _v)
		{
			for(int i = 0; i < 8; ++i) _d[_off + i] = static_cast<uint8_t>(_v >> (56 - 8 * i));
		}
	}

	uint64_t Pattern::trigs(const uint8_t _track) const { return getU64(data, g_ampTrigs + _track * 8); }

	void Pattern::setTrigs(const uint8_t _track, const uint64_t _mask)
	{
		setU64(data, g_ampTrigs + _track * 8, _mask);
		setU64(data, g_filterTrigs + _track * 8, _mask);
		setU64(data, g_lfoTrigs + _track * 8, _mask);
		setU64(data, g_offTrigs + _track * 8, 0);
		setU64(data, g_triglessTrigs + _track * 8, 0);
		setU64(data, g_chordTrigs + _track * 8, 0);
	}

	uint8_t Pattern::note(const uint8_t _track, const uint8_t _step) const { return data[g_notes + _track * 64 + _step]; }
	void Pattern::setNote(const uint8_t _track, const uint8_t _step, const uint8_t _note) { data[g_notes + _track * 64 + _step] = _note & 0x7f; }
	size_t Pattern::stepCount() const { return std::clamp<size_t>(data[g_patternLength], 1, g_steps); }
	uint64_t Pattern::lockMask(const uint8_t _track) const { return getU64(data, g_lockPatterns + _track * 8); }
	bool Pattern::hasLock(const uint8_t _track, const uint8_t _param) const
	{
		return _track < g_tracks && _param < 64 && (lockMask(_track) >> _param & 1u);
	}

	size_t Pattern::rowIndex(const uint8_t _track, const uint8_t _param) const
	{
		size_t index = 0;
		for(uint8_t t = 0; t < g_tracks; ++t)
		{
			const auto mask = lockMask(t);
			for(uint8_t p = 0; p < 64; ++p)
			{
				if(t == _track && p == _param) return index;
				if(mask >> p & 1u) ++index;
			}
		}
		return index;
	}

	size_t Pattern::rowCount() const
	{
		size_t n = 0;
		for(uint8_t t = 0; t < g_tracks; ++t)
			n += static_cast<size_t>(__builtin_popcountll(lockMask(t)));
		return n;
	}

	uint8_t* Pattern::row(const size_t _index) { return data.data() + g_locks + _index * 64; }

	bool Pattern::setLock(const uint8_t _track, const uint8_t _param, const uint8_t _step, const uint8_t _value)
	{
		if(_track >= g_tracks || _param >= 64 || _step >= g_steps)
			return false;
		if(!hasLock(_track, _param))
		{
			const auto count = rowCount();
			if(count >= g_maxRows)
				return false;
			const auto index = rowIndex(_track, _param);
			// Shift rows [index, count) down by one and blank the new row.
			std::memmove(row(index + 1), row(index), (count - index) * 64);
			std::memset(row(index), g_noLock, 64);
			setU64(data, g_lockPatterns + _track * 8, lockMask(_track) | 1ull << _param);
			data[g_locksUsed] = static_cast<uint8_t>(count + 1);
		}
		row(rowIndex(_track, _param))[_step] = _value;
		return true;
	}

	void Pattern::clearTrackLocks(const uint8_t _track)
	{
		for(uint8_t param = 64; param-- > 0;)
		{
			if(!hasLock(_track, param))
				continue;
			const auto count = rowCount();
			const auto index = rowIndex(_track, param);
			std::memmove(row(index), row(index + 1), (count - index - 1) * 64);
			std::memset(row(count - 1), g_noLock, 64);
			setU64(data, g_lockPatterns + _track * 8, lockMask(_track) & ~(1ull << param));
			data[g_locksUsed] = static_cast<uint8_t>(count - 1);
		}
	}

	std::vector<uint8_t> request(const uint8_t _pattern)
	{
		return {0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x68, static_cast<uint8_t>(_pattern & 0x7f), 0xf7};
	}

	bool isPatternDump(const std::vector<uint8_t>& _m)
	{
		return _m.size() >= 15 && _m[0] == 0xf0 && _m[1] == 0x00 && _m[2] == 0x20 && _m[3] == 0x3c
			&& _m[4] == 0x03 && _m[5] == 0x00 && _m[6] == 0x67 && _m.back() == 0xf7;
	}

	std::optional<Pattern> decode(const std::vector<uint8_t>& _m, std::string* _error)
	{
		auto fail = [&](const char* _what) { if(_error) *_error = _what; return std::optional<Pattern>{}; };
		if(!isPatternDump(_m))
			return fail("not a Monomachine pattern dump");
		if(std::any_of(_m.begin() + 1, _m.end() - 1, [](const uint8_t _v) { return _v > 0x7f; }))
			return fail("malformed sysex byte");
		const auto checksumPos = _m.size() - 5;
		uint32_t sum = 0;
		for(size_t i = 9; i < checksumPos; ++i) sum += _m[i];
		if((sum & 0x3fff) != static_cast<uint32_t>(_m[checksumPos] << 7 | _m[checksumPos + 1]))
			return fail("bad checksum");
		if(static_cast<size_t>(_m[checksumPos + 2] << 7 | _m[checksumPos + 3]) != _m.size() - 10)
			return fail("bad length");
		const auto expanded = rleExpand(unpack7(_m.data() + 10, _m.data() + checksumPos));
		if(!expanded)
			return fail("bad RLE stream");
		if(expanded->size() != g_decodedSize)
		{
			if(_error) *_error = "unexpected decoded size " + std::to_string(expanded->size());
			return std::nullopt;
		}
		Pattern p;
		p.version = _m[7]; p.revision = _m[8]; p.position = _m[9];
		p.data = *expanded;
		return p;
	}

	std::vector<uint8_t> encode(const Pattern& _p)
	{
		std::vector<uint8_t> m = {0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x67, _p.version, _p.revision, _p.position};
		pack7(m, rleCompress(_p.data));
		uint32_t sum = 0;
		for(size_t i = 9; i < m.size(); ++i) sum += m[i];
		const auto length = m.size() - 5;
		m.push_back(static_cast<uint8_t>(sum >> 7 & 0x7f));
		m.push_back(static_cast<uint8_t>(sum & 0x7f));
		m.push_back(static_cast<uint8_t>(length >> 7 & 0x7f));
		m.push_back(static_cast<uint8_t>(length & 0x7f));
		m.push_back(0xf7);
		return m;
	}
}
