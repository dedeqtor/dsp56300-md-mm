#pragma once

#include "types.h"

namespace dsp56k
{
	class DSP;

	class DspBoot
	{
	public:
		enum class State
		{
			Length,
			Address,
			Data,
			Finished
		};

		explicit DspBoot(DSP& _dsp);

		bool hdiWriteTX(const TWord& _val);

		bool finished() const { return m_state == State::Finished; }

		auto getLength() const { return m_length; }
		auto getInitialPC() const { return m_initialPC; }

		template<typename TStream> void serializeState(TStream& _s)
		{
			_s.marker(0x424f4f54);	// BOOT
			_s(m_state, m_length, m_initialPC, m_remaining, m_address);
		}

	private:
		DSP& m_dsp;

		State m_state = State::Length;

		TWord m_length = 0;
		TWord m_initialPC = 0;

		TWord m_remaining = 0;
		TWord m_address = 0;
	};
}
