#pragma once

#include <memory>

#include "types.h"

#include <set>
#include <vector>
#include <unordered_map>

#include "debuggerinterface.h"

#include "jitblockchain.h"
#include "jitblockinfo.h"
#include "jitcacheentry.h"
#include "jitconfig.h"
#include "jitdspmode.h"
#include "jitruntimedata.h"
#include "jittrampoline.h"

namespace asmjit
{
	inline namespace ASMJIT_ABI_NAMESPACE
	{
		class JitRuntime;
		class CodeHolder;
	}
}

namespace dsp56k
{
	struct DspRegs;
	struct JitBlockInfo;
	class DSP;
	class JitBlock;
	class JitProfilingSupport;
	struct JitBlockEmitter;

	class Jit final
	{
	public:
		explicit Jit(DSP& _dsp);
		~Jit();

		DSP& dsp() { return m_dsp; }

		void exec(const TWord _pc)
		{
			m_currentChain->exec(_pc);
		}

		// Make sure _pc can be dispatched, i.e. that the JIT dispatch table covers it. Only has an effect for the
		// non-MMU fallback array, which is grown on demand; the MMU-backed array already spans all of P memory.
		// Used by DSP::onInvalidPC to tell "table not grown yet" (recoverable) from "PC outside P memory" (a bug).
		bool ensurePcDispatchable(const TWord _pc) const
		{
			if(!m_currentChain)
				return false;
			m_currentChain->setMaxUsedPAddress(_pc);
			return _pc < m_currentChain->getFuncSize();
		}

		static Jit* toJitPtr(DspRegs* _regs);
		void notifyProgramMemWrite(const TWord _offset);

		void run(TWord _pc) noexcept;
		void runCheckPMemWrite(TWord _pc) noexcept;
		void runCheckPMemWriteAndModeChange(TWord _pc) noexcept;
		void runCheckModeChange(TWord _pc) noexcept;

		const JitConfig& getConfig() const { return m_config; }
		JitConfig getConfig(TWord _pc) const;
		void setConfig(const JitConfig& _config) { m_config = _config; }
		void resetHW();
		const std::map<TWord, TWord>& getLoops() const { return m_loops; }
		const std::set< TWord>& getLoopEnds() const { return m_loopEnds; }

		static TJitFunc updateRunFunc(const JitCacheEntry& e);

		auto* getRuntime() { return m_rt; }
		auto& getRuntimeData() { return m_runtimeData; }
		const auto& getVolatileP()  { return m_volatileP; }
		auto* getProfilingSupport() const { return m_profiling.get(); }

		bool isVolatileP(const TWord _pc) const
		{
			return m_volatileP.find(_pc) != m_volatileP.end();
		}

		void create(TWord _pc, bool _execute);
		void recreate(TWord _pc);

		void addLoop(const JitBlockInfo& _info);
		void addLoop(TWord _begin, TWord _end);
		void removeLoop(const JitBlockInfo& _info);
		void removeLoop(TWord _begin);

		void destroy(TWord _pc);
		void destroyToRecreate(TWord _pc);

		void checkModeChange() noexcept;
		const JitBlockInfo* getBlockInfo(TWord _pc) const noexcept;

		void onDebuggerAttached(DebuggerInterface& _debugger) const;

		void destroyAllBlocks();

		JitBlockEmitter* acquireEmitter(JitConfig&& _config);
		JitBlockEmitter* acquireEmitter(TWord _pc);
		void releaseEmitter(JitBlockEmitter* _emitter);

		JitBlockRuntimeData* acquireBlockRuntimeData();
		void releaseBlockRuntimeData(JitBlockRuntimeData* _b);
		// Populate the free pool to at least _count objects.
		void preallocateBlockRuntimeData(size_t _count);

		void onFuncsResized(const JitBlockChain& _chain) const;

		JitTrampoline& getTrampoline() { return m_trampoline; }
		const CowMemory& getDispatchTemplate() const { return m_dispatchTemplate; }

		void setForcedBlockLayout(const JitBlockLayout* _layout) { m_forcedBlockLayout = _layout; }
		const JitBlockLayout* getForcedBlockLayout(const TWord _pc) const
		{
			return m_forcedBlockLayout && m_forcedBlockLayout->pc == _pc ? m_forcedBlockLayout : nullptr;
		}

		struct LoopState
		{
			TWord begin = 0;
			TWord end = 0;
		};

		struct ChainState
		{
			uint32_t mode = JitDspMode::Uninitialized;
			std::vector<JitBlockLayout> blocks;
			std::vector<TWord> words;		// source words of blocks with wordCount != 0, in block order

			template<typename TStream> void serializeState(TStream& _s)
			{
				_s(mode, blocks, words);
			}
		};

		struct State
		{
			std::vector<TWord> volatileP;
			std::vector<LoopState> loops;
			std::vector<TWord> loopEnds;
			uint64_t maxUsedPAddress = 0;
			std::vector<ChainState> chains;

			template<typename TStream> void serializeState(TStream& _s)
			{
				_s.marker(0x4a495430);	// JIT0
				_s(volatileP, loops, loopEnds, maxUsedPAddress, chains);
			}
		};

		// Captures the compiled block layout of all DSP modes. Blocks are not position independent and are
		// recompiled on restore, but their boundaries and flags depend on the order in which they were created,
		// which affects when peripherals and interrupts are processed. Restoring recreates identical blocks.
		void getState(State& _state);

		// Destroys all blocks and recompiles the recorded ones. P memory and DSP registers must be restored
		// already. Returns the number of blocks that could not be recreated identically
		uint32_t setState(const State& _state);

		template<typename TStream> void serializeState(TStream& _s)
		{
			State state;
			if constexpr (!TStream::Reading)
				getState(state);
			_s(state);
			if constexpr (TStream::Reading)
			{
				if(_s.failed())
				{
					destroyAllBlocks();
					return;
				}
				setState(state);
			}
		}

	private:
		void checkPMemWrite() noexcept;

		DSP& m_dsp;
		JitTrampoline m_trampoline;

		asmjit::ASMJIT_ABI_NAMESPACE::JitRuntime* m_rt = nullptr;

		struct DspModeHash
		{
		    std::size_t operator () (const JitDspMode& m) const	{ return m.get(); }
		};

		std::unordered_map<JitDspMode, std::unique_ptr<JitBlockChain>, DspModeHash> m_chains;
		JitBlockChain* m_currentChain = nullptr;

		CowMemory m_dispatchTemplate;
		std::set<TWord> m_volatileP;
		std::map<TWord, TWord> m_loops;
		std::set<TWord> m_loopEnds;

		std::unique_ptr<JitProfilingSupport> m_profiling;

		std::vector<JitBlockEmitter*> m_emitters;
		std::vector<JitBlockRuntimeData*> m_blockRuntimeDatas;

		JitConfig m_config;

		size_t m_maxUsedPAddress = 0;

		const JitBlockLayout* m_forcedBlockLayout = nullptr;

		// the following data is accessed by JIT code at runtime, it NEEDS to be put last into this struct to be
		// able to use ARM relative addressing, see member ordering in dsp.h
		JitRuntimeData m_runtimeData;
	};
}
