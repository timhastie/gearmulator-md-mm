#include "mdpatterndump.h"

#include <algorithm>

namespace md::patternDump
{
	namespace
	{
		// 7-bit "8-in-7" packing: each group of seven data bytes becomes one
		// MSB byte followed by the seven low halves. A partial group is flushed
		// as (n + 1) wire bytes.
		std::vector<uint8_t> unpack7(const uint8_t* _begin, const uint8_t* _end)
		{
			std::vector<uint8_t> out;
			out.reserve(static_cast<size_t>(_end - _begin));
			for(const uint8_t* p = _begin; p < _end;)
			{
				const auto msb = *p++;
				for(uint8_t i = 0; i < 7 && p < _end; ++i, ++p)
					out.push_back(static_cast<uint8_t>(*p | ((msb >> (6 - i)) & 1u) << 7));
			}
			return out;
		}

		void pack7(std::vector<uint8_t>& _out, const uint8_t* _begin, const uint8_t* _end)
		{
			for(const uint8_t* p = _begin; p < _end;)
			{
				const auto n = std::min<size_t>(7, static_cast<size_t>(_end - p));
				uint8_t msb = 0;
				for(size_t i = 0; i < n; ++i)
					msb |= static_cast<uint8_t>((p[i] >> 7) << (6 - i));
				_out.push_back(msb);
				for(size_t i = 0; i < n; ++i)
					_out.push_back(p[i] & 0x7f);
				p += n;
			}
		}

		constexpr size_t wireSize(const size_t _dataBytes)
		{
			return (_dataBytes / 7) * 8 + (_dataBytes % 7 ? _dataBytes % 7 + 1 : 0);
		}

		std::optional<std::vector<uint8_t>> rleExpand(const std::vector<uint8_t>& _in)
		{
			std::vector<uint8_t> out;
			for(size_t i = 0; i < _in.size(); ++i)
			{
				if(_in[i] < 0x80)
				{
					out.push_back(_in[i]);
					continue;
				}
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
				while(i + run < _in.size() && _in[i + run] == _in[i] && run < 0x7f)
					++run;
				if(run == 1 && _in[i] < 0x80)
					out.push_back(_in[i]);
				else
				{
					out.push_back(static_cast<uint8_t>(0x80 | run));
					out.push_back(_in[i]);
				}
				i += run;
			}
			return out;
		}

		struct Reader
		{
			const std::vector<uint8_t>& d;
			size_t pos = 0;
			bool ok = true;
			uint8_t u8() { if(pos >= d.size()) { ok = false; return 0; } return d[pos++]; }
			uint32_t u32() { uint32_t v = 0; for(int i = 0; i < 4; ++i) v = v << 8 | u8(); return v; }
			void bytes(uint8_t* _dst, const size_t _n) { for(size_t i = 0; i < _n; ++i) _dst[i] = u8(); }
		};

		struct Writer
		{
			std::vector<uint8_t> d;
			void u8(const uint8_t _v) { d.push_back(_v); }
			void u32(const uint32_t _v) { for(int s = 24; s >= 0; s -= 8) u8(static_cast<uint8_t>(_v >> s)); }
			void bytes(const uint8_t* _src, const size_t _n) { d.insert(d.end(), _src, _src + _n); }
		};

		uint64_t setLow(const uint64_t _v, const uint32_t _lo) { return (_v & 0xffffffff00000000ull) | _lo; }
		uint64_t setHigh(const uint64_t _v, const uint32_t _hi) { return (_v & 0xffffffffull) | static_cast<uint64_t>(_hi) << 32; }

		size_t extensionMaskOffset(const Pattern& _p)
		{
			return g_tracks * (64 + 64 + _p.lockSlotCount) + 16 + 16;
		}

		bool fail(std::string* _error, const char* _what)
		{
			if(_error) *_error = _what;
			return false;
		}
	}

	size_t Pattern::stepCount() const
	{
		return std::clamp<size_t>(patternLength, 1, g_maxSteps);
	}

	size_t Pattern::rowIndex(const uint8_t _track, const uint8_t _param) const
	{
		size_t index = 0;
		for(uint8_t t = 0; t < g_tracks; ++t)
		{
			for(size_t p = 0; p < lockSlotCount; ++p)
			{
				if(t == _track && p == _param)
					return index;
				if(lockMasks[t] >> p & 1u)
					++index;
			}
		}
		return index;
	}

	bool Pattern::hasLock(const uint8_t _track, const uint8_t _param) const
	{
		return _track < g_tracks && _param < lockSlotCount && (lockMasks[_track] >> _param & 1u);
	}

	bool Pattern::setLock(const uint8_t _track, const uint8_t _param, const uint8_t _step, const uint8_t _value)
	{
		if(_track >= g_tracks || _param >= lockSlotCount || _step >= g_maxSteps)
			return false;
		if(!hasLock(_track, _param))
		{
			if(rows.size() >= g_maxRows)
				return false;
			Row row;
			row.fill(g_noLock);
			rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(rowIndex(_track, _param)), row);
			lockMasks[_track] |= 1ull << _param;
		}
		rows[rowIndex(_track, _param)][_step] = _value;
		return true;
	}

	std::vector<uint8_t> request(const uint8_t _pattern)
	{
		return {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, g_patternRequest,
			static_cast<uint8_t>(_pattern & 0x7f), 0xf7};
	}

	bool isPatternDump(const std::vector<uint8_t>& _m)
	{
		return _m.size() >= 15 && _m[0] == 0xf0 && _m[1] == 0x00 && _m[2] == 0x20
			&& _m[3] == 0x3c && _m[4] == 0x02 && _m[5] == 0x00 && _m[6] == g_patternDump
			&& _m.back() == 0xf7;
	}

	std::optional<Pattern> decode(const std::vector<uint8_t>& _m, std::string* _error)
	{
		if(!isPatternDump(_m))
		{
			fail(_error, "not a pattern dump");
			return std::nullopt;
		}
		if(std::any_of(_m.begin() + 1, _m.end() - 1, [](const uint8_t _v) { return _v > 0x7f; }))
		{
			fail(_error, "malformed sysex byte");
			return std::nullopt;
		}
		const auto checksumPos = _m.size() - 5;
		uint32_t sum = 0;
		for(size_t i = 9; i < checksumPos; ++i)
			sum += _m[i];
		if((sum & 0x3fff) != static_cast<uint32_t>(_m[checksumPos] << 7 | _m[checksumPos + 1]))
		{
			fail(_error, "bad checksum");
			return std::nullopt;
		}
		if(static_cast<size_t>(_m[checksumPos + 2] << 7 | _m[checksumPos + 3]) != _m.size() - 10)
		{
			fail(_error, "bad length");
			return std::nullopt;
		}

		Pattern p;
		p.version = _m[7];
		p.revision = _m[8];
		p.position = _m[9];
		p.lockSlotCount = p.version == 0x41 ? 37 : p.version == 0x40 ? 34 : g_classicParams;

		const uint8_t* cursor = _m.data() + 10;
		const uint8_t* const end = _m.data() + checksumPos;
		auto segment = [&](const size_t _dataBytes) -> std::optional<std::vector<uint8_t>>
		{
			const auto wire = wireSize(_dataBytes);
			if(static_cast<size_t>(end - cursor) < wire)
				return std::nullopt;
			auto data = unpack7(cursor, cursor + wire);
			cursor += wire;
			if(data.size() != _dataBytes)
				return std::nullopt;
			return data;
		};

		const auto s1 = segment(64), s2 = segment(64), s3 = segment(16);
		if(!s1 || !s2 || !s3 || end - cursor < 6)
		{
			fail(_error, "truncated header blocks");
			return std::nullopt;
		}
		{
			Reader r{*s1};
			for(auto& t : p.trigs) t = r.u32();
			Reader r2{*s2};
			for(auto& t : p.lockMasks) t = r2.u32();
			Reader r3{*s3};
			p.accent = r3.u32(); p.slide = r3.u32(); p.swing = r3.u32(); p.swingAmount = r3.u32();
		}
		p.accentAmount = *cursor++; p.patternLength = *cursor++; p.doubleTempo = *cursor++;
		p.scale = *cursor++; p.kit = *cursor++; p.numLockedRows = *cursor++;
		if(p.patternLength == 0 || p.patternLength > g_maxSteps)
		{
			fail(_error, "bad pattern length");
			return std::nullopt;
		}
		const auto s4 = segment(g_maxRows * 32), s5 = segment(12 + 3 * 64);
		if(!s4 || !s5)
		{
			fail(_error, "truncated lock blocks");
			return std::nullopt;
		}
		std::array<Row, g_maxRows> rawRows{};
		{
			Reader r{*s4};
			for(auto& row : rawRows) { row.fill(g_noLock); r.bytes(row.data(), 32); }
			Reader r5{*s5};
			for(auto& e : p.editAll) e = r5.u32();
			for(auto& t : p.accentPatterns) t = r5.u32();
			for(auto& t : p.slidePatterns) t = r5.u32();
			for(auto& t : p.swingPatterns) t = r5.u32();
		}

		p.extra = static_cast<size_t>(end - cursor) >= wireSize(64 + 12 + g_maxRows * 32 + 3 * 64)
			&& (p.patternLength > 32 || p.lockSlotCount == g_classicParams);
		if(p.extra)
		{
			const auto s6 = segment(64 + 12 + g_maxRows * 32 + 3 * 64);
			if(!s6)
			{
				fail(_error, "truncated 64-step block");
				return std::nullopt;
			}
			Reader r{*s6};
			for(auto& t : p.trigs) t = setHigh(t, r.u32());
			p.accent = setHigh(p.accent, r.u32()); p.slide = setHigh(p.slide, r.u32()); p.swing = setHigh(p.swing, r.u32());
			for(auto& row : rawRows) r.bytes(row.data() + 32, 32);
			for(auto& t : p.accentPatterns) t = setHigh(t, r.u32());
			for(auto& t : p.slidePatterns) t = setHigh(t, r.u32());
			for(auto& t : p.swingPatterns) t = setHigh(t, r.u32());
		}

		if(cursor < end)
		{
			if(p.lockSlotCount == g_classicParams)
			{
				fail(_error, "unexpected trailing data");
				return std::nullopt;
			}
			const auto expanded = rleExpand(unpack7(cursor, end));
			const auto maskOffset = extensionMaskOffset(p);
			if(!expanded || expanded->size() < maskOffset + 64 + 2 + 3)
			{
				fail(_error, "bad extension block");
				return std::nullopt;
			}
			p.hasExtension = true;
			p.extension = *expanded;
			Reader r{p.extension};
			r.pos = maskOffset;
			for(auto& t : p.lockMasks) t = setHigh(t, r.u32());
			const auto lo = r.u8(), hi = r.u8();
			const size_t totalRows = lo | hi << 8;
			if(totalRows > g_maxRows)
			{
				fail(_error, "pattern uses more than 64 lock rows");
				return std::nullopt;
			}
		}

		size_t rowCount = 0;
		for(uint8_t t = 0; t < g_tracks; ++t)
			for(size_t q = 0; q < p.lockSlotCount; ++q)
				if(p.lockMasks[t] >> q & 1u)
					++rowCount;
		if(rowCount > g_maxRows)
		{
			fail(_error, "pattern uses more than 64 lock rows");
			return std::nullopt;
		}
		p.rows.assign(rawRows.begin(), rawRows.begin() + static_cast<std::ptrdiff_t>(rowCount));
		return p;
	}

	std::vector<uint8_t> encode(const Pattern& _p)
	{
		std::vector<uint8_t> m = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, g_patternDump,
			_p.version, _p.revision, _p.position};

		auto emit = [&](const Writer& _w) { pack7(m, _w.d.data(), _w.d.data() + _w.d.size()); };
		Writer w;
		for(const auto t : _p.trigs) w.u32(static_cast<uint32_t>(t));
		emit(w); w.d.clear();
		for(const auto t : _p.lockMasks) w.u32(static_cast<uint32_t>(t));
		emit(w); w.d.clear();
		w.u32(static_cast<uint32_t>(_p.accent)); w.u32(static_cast<uint32_t>(_p.slide));
		w.u32(static_cast<uint32_t>(_p.swing)); w.u32(_p.swingAmount);
		emit(w); w.d.clear();

		const auto rowCount = std::min(_p.rows.size(), g_maxRows);
		m.push_back(_p.accentAmount); m.push_back(_p.patternLength); m.push_back(_p.doubleTempo);
		m.push_back(_p.scale); m.push_back(_p.kit); m.push_back(static_cast<uint8_t>(std::min<size_t>(rowCount, 0x7f)));

		Row blank;
		blank.fill(g_noLock);
		for(size_t i = 0; i < g_maxRows; ++i)
			w.bytes((i < rowCount ? _p.rows[i] : blank).data(), 32);
		emit(w); w.d.clear();
		for(const auto e : _p.editAll) w.u32(e);
		for(const auto t : _p.accentPatterns) w.u32(static_cast<uint32_t>(t));
		for(const auto t : _p.slidePatterns) w.u32(static_cast<uint32_t>(t));
		for(const auto t : _p.swingPatterns) w.u32(static_cast<uint32_t>(t));
		emit(w); w.d.clear();

		if(_p.extra)
		{
			for(const auto t : _p.trigs) w.u32(static_cast<uint32_t>(t >> 32));
			w.u32(static_cast<uint32_t>(_p.accent >> 32)); w.u32(static_cast<uint32_t>(_p.slide >> 32));
			w.u32(static_cast<uint32_t>(_p.swing >> 32));
			for(size_t i = 0; i < g_maxRows; ++i)
				w.bytes((i < rowCount ? _p.rows[i] : blank).data() + 32, 32);
			for(const auto t : _p.accentPatterns) w.u32(static_cast<uint32_t>(t >> 32));
			for(const auto t : _p.slidePatterns) w.u32(static_cast<uint32_t>(t >> 32));
			for(const auto t : _p.swingPatterns) w.u32(static_cast<uint32_t>(t >> 32));
			emit(w); w.d.clear();
		}

		if(_p.hasExtension)
		{
			auto ext = _p.extension;
			Writer masks;
			for(const auto t : _p.lockMasks) masks.u32(static_cast<uint32_t>(t >> 32));
			masks.u8(static_cast<uint8_t>(rowCount & 0xff));
			masks.u8(static_cast<uint8_t>(rowCount >> 8));
			std::copy(masks.d.begin(), masks.d.end(), ext.begin() + static_cast<std::ptrdiff_t>(extensionMaskOffset(_p)));
			const auto rle = rleCompress(ext);
			pack7(m, rle.data(), rle.data() + rle.size());
		}

		uint32_t sum = 0;
		for(size_t i = 9; i < m.size(); ++i)
			sum += m[i];
		const auto length = m.size() + 4 - 9;	// bytes from position through length field
		m.push_back(static_cast<uint8_t>(sum >> 7 & 0x7f));
		m.push_back(static_cast<uint8_t>(sum & 0x7f));
		m.push_back(static_cast<uint8_t>(length >> 7 & 0x7f));
		m.push_back(static_cast<uint8_t>(length & 0x7f));
		m.push_back(0xf7);
		return m;
	}
}
